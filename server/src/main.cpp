// mu2_server: MU2_BGFX's server (docs/sprints/18-the-wire.md, server/README.md).
//
//   mu2_server [--port N] [--assets DIR] [--store FILE] [--castle-period S] [--square-period S] [--map-cap N] [--speed N]
//
// **Connections to the same world share it** (docs/sprints/19-many-heroes.md): the first Hello
// for a world raises its realm with him as its first player; each later one comes in by a Join
// command at the next tick's start, and is welcomed once that tick has let him in -- with the
// world's start, its last snapshot and every tick since that (at most a minute's, sprint 21),
// which his mirror is laid with and replays to stand where the
// server stands. A connection that goes leaves by a Leave command, and his character (sim::Kept)
// is kept under his token in characters.db (store.h): a map change reconnects with it, and he
// comes into the next world as he left the last -- or, next week, as he left the server
// (docs/sprints/20-the-world-host.md). A world nobody is in is let go. Every
// realm steps on one 20 Hz deadline: poll, step every world, flush (server-plan §3).
// **Where he comes into the next world is the server's:** a gate, a Tab trip, a way home or the
// end of Blood Castle is read off what the realm said, and the Hello that follows is put down
// there, whatever tile the client asked for (landingOf). And the store keeps which world he is
// in: a run's first Hello that names another is answered Elsewhere.
// Each tick goes to everyone in the world -- the wall clock, the rain, the commands applied, each
// with its player -- and every second its hash, which a mirror that disagrees says.
//
// **The character screen is the server's too** (phase 6, docs/sprints/23-the-account.md): a
// connection that opens with Account is answered with the account's characters (Roster), and may
// then Create and Delete; each is answered with the Roster again. An account is a key the client
// made once, and its characters are played only with it.
//
// A malformed frame drops that connection, never the server.

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

namespace {

using namespace mu;

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

using Clock = std::chrono::steady_clock;
constexpr double kTickSeconds = 0.05;  // 20 Hz, the realm's own (sim/realm.h)
// A tick's wall time: 50 ms, or less under --speed N, a test's world run N times as fast (the
// Claude bot's, tools/netbot/claude.h: "you need to find a way how massively to speed up
// things"). The clients follow whatever ticks come; nothing in the realm reads the wall.
Clock::duration g_tick = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kTickSeconds));
// **And the wall clock the ticks carry, N times as fast** from the server's start: Blood Castle's
// door and a quest's twelve-hour rest read it (Realm::wallClock), and at --speed 100 a castle
// party stood at the Messenger for game hours while the door kept real minutes.
int g_speed = 1;
const int64_t g_wallStart = int64_t(std::time(nullptr));
int64_t wallNow() { return g_wallStart + (int64_t(std::time(nullptr)) - g_wallStart) * g_speed; }
// An order that sets a standing player off takes his world's tick at once, and the world's clock
// starts again from there: what the client does for a click on its own (game/play.cpp), done
// where the ticks are made. It was 0 to 50 ms, 25 on average, between his order reaching the
// server and the tick that carries it -- the one wait in the walk the line does not set.
// Invention, and it runs the world a little fast: one interval cut short at most this often
// per world, so never more than 1/20 faster (the user, 2026-10-08, agreed). Not when the
// deadline is nearly here anyway (kEarlyWithin): that would cut short for nothing.
constexpr double kEarlyApart = 1.0;
const Clock::duration kEarlyWithin = std::chrono::milliseconds(5);
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
server::Store g_store;
// The item rows a Roster's worn things are read against, and a new character's cradle weapon
// found in: Lorencia's, as the client's screen dresses its pedestals (an item's row is the same
// in every world's tables).
content::Tables g_items;

// Where a character is due next, by token: set when the realm sends him to another world, and
// written as where he is when his connection goes (placeOf).
struct Landing {
    std::string world;
    int column = 0, row = 0;
    int castle = 0;  // into Blood Castle: which one his ticket passed him into
    bool summon = false;  // her summon comes too: a Tab trip's (Realm::travel), no gate's
    int square = 0, squareWait = 0;  // into Devil Square: which, and the seconds of Charon's gate left
};
std::map<uint64_t, Landing> g_due;
// Tests (2026-10-08): `--storm` raises every world in a wet spell, `--invasion` begins the Golden
// Invasion as a world is raised on a map that has one -- so a client joining mid-storm and
// mid-invasion can be tried at once rather than after a dry spell and the dice.
bool g_storm = false;
bool g_invasion = false;
// A test (2026-10-11, Devil Square's server path): `--kit LEVEL:STEM[+N],...` makes every new
// character at that level with those things in his bag, carried in as one from another world is,
// so Charon's door can be played on a server without a day of levelling first. 0 for none.
int g_kitLevel = 0;
std::vector<std::pair<std::string, int>> g_kitItems;  // stem, plus
// The most players in one copy of a map before the next is opened (`--map-cap N`): where the
// client's own cost tells -- every client steps the whole map -- about 100 by the measurement of
// 2026-10-08 (docs/sprints/24-the-quiet-line.md).
int g_mapCap = 100;
// The tick datagrams' socket, on the game's port; closed if it would not bind, and then the
// stream alone carries the ticks, as before sprint 24.
net::Datagram g_udp;
// How many recent ticks a datagram may carry: four, a fifth of a second of a line's losses.
constexpr size_t kRecentTicks = 4;

// ---- the server's own timing (docs/sprints/25-the-clear-thread.md) -----------------------------
// One line a minute of how the loop kept time, and one for any pass over kSlowPass naming where
// it went: so a player's "lag" can be laid at the server's door, or not. The client logs its own
// side (RemoteLink's slow answers, Play's long frames).
constexpr double kSlowPass = 10.0;  // ms
constexpr auto kStatsEvery = std::chrono::seconds(60);
double msSince(Clock::time_point at) { return std::chrono::duration<double, std::milli>(Clock::now() - at).count(); }
// This thread's own CPU time, in ms: a long pass that used little of it was waiting for the
// machine -- another process, the host's steal on a shared box -- not working.
double cpuMs() {
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
} g_stats;

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
    // The chat's allowance (sayAllowed): lines he may say at once, refilled with time.
    float sayAllowance = 0.0f;
    Clock::time_point saidAt{};
    // When anything last came in on his line. Every client pings at least every 5 s
    // (RemoteLink::pump), so one silent for kSilentFor is a dead line his next Hello may take.
    Clock::time_point heardAt = Clock::now();
    // Commands taken from him for the coming tick (kMostCommands), counted afresh each tick.
    int asked = 0;
};

// Why his line is closing, to a player in a world, before it closes (net::Bye): the client's
// Disconnected window says it. One still joining is not told: RemoteLink::join reads a refusal
// as it always has. Sent behind whatever was owed him, so a line too far behind may never see it.
void bye(Session& one, net::Why why) {
    if (one.welcomed && one.socket.open()) {
        std::vector<uint8_t> out;
        net::put(out, net::Bye{why});
        if (one.socket.send(out)) one.socket.flush();
    }
    one.socket.close();
}

// Who is in a world, to everyone in it: what the hover plate over another player says. Sent
// after every welcome, so a newcomer learns the rest and the rest learn him.
void announce(const World& world, const std::vector<std::unique_ptr<Session>>& sessions) {
    net::Who who;
    for (const auto& one : sessions) {
        if (one->world == &world && one->welcomed && one->player != 0) {
            who.players.push_back({one->player, one->name, one->bot});
        }
    }
    std::vector<uint8_t> out;
    net::put(out, who);
    for (const auto& one : sessions) {
        if (one->world == &world && one->welcomed && one->socket.open() && !one->socket.send(out)) {
            one->socket.close();
        }
    }
}

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
std::map<uint32_t, Party> g_parties;
std::map<uint64_t, uint32_t> g_partyOf;  // a member's token -> his party
// The latest invite to a player, by his token: who asked, and when; it stands kInviteFor.
struct PartyInvite {
    uint64_t from = 0;
    Clock::time_point at{};
};
std::map<uint64_t, PartyInvite> g_invites;
// Where each member was last put in a party in a realm: his world and body, so a new one (a map
// change) is told again. And since when a member has been on no connection.
std::map<uint64_t, std::pair<const World*, uint32_t>> g_partyBody;
std::map<uint64_t, Clock::time_point> g_partyAway;
uint32_t g_nextParty = 1;
constexpr auto kInviteFor = std::chrono::seconds(60);
constexpr auto kPartyAway = std::chrono::seconds(30);

// ---- the chat ---------------------------------------------------------------------------------
// Lines said on a channel (net::Say) passed on as net::Heard, outside the lockstep as Who is: the
// realm knows no names. Each line goes to the server's log too.

// A burst of five lines, then one each 1.5 s: enough to talk, too little to flood a town.
constexpr float kSayBurst = 5.0f, kSayEvery = 1.5f;

void hearLine(Session& one, const net::Heard& line) {
    if (!one.welcomed || !one.socket.open()) return;
    std::vector<uint8_t> out;
    net::put(out, line);
    if (!one.socket.send(out)) one.socket.close();
}

// The server's own line to one player, on a channel: "Valeria has joined the party." on Party.
void chatSystem(Session& one, net::Channel channel, const std::string& text) {
    net::Heard line;
    line.channel = channel;
    line.text = text;
    hearLine(one, line);
}

// The welcomed sessions in his party, himself first, on any map. The party's section owns this
// (docs/party.md); until a party exists he is a party of one.
std::vector<Session*> partyMates(Session& one, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto at = g_partyOf.find(one.token);
    if (at == g_partyOf.end()) return {&one};
    std::vector<Session*> out{&one};
    for (const Party::Member& m : g_parties[at->second].members) {
        if (m.token == one.token) continue;
        for (auto& other : sessions) {
            if (other->token == m.token && other->welcomed && other->socket.open()) {
                out.push_back(other.get());
                break;
            }
        }
    }
    return out;
}

// The welcomed player called `name` on any map, in any case, or nullptr.
Session* playerNamed(const std::string& name, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(uint8_t(c)));
        return s;
    };
    const std::string wanted = lower(name);
    for (auto& one : sessions) {
        if (one->welcomed && one->socket.open() && lower(one->name) == wanted) return one.get();
    }
    return nullptr;
}

// Whether anyone on the server but `except`, in a world or on his way into one, goes by `name`,
// in any case: a name is the whole of how chat, whispers and invites find a player.
bool nameInUse(const std::string& name, const std::vector<std::unique_ptr<Session>>& sessions,
               const Session* except) {
    const auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(uint8_t(c)));
        return s;
    };
    const std::string wanted = lower(name);
    for (const auto& one : sessions) {
        if (one.get() != except && one->world != nullptr && one->socket.open() && lower(one->name) == wanted) {
            return true;
        }
    }
    return false;
}

// Whether he may say another line now: the allowance refilled since his last, and one spent.
bool sayAllowed(Session& one) {
    const Clock::time_point now = Clock::now();
    if (one.saidAt == Clock::time_point{}) one.sayAllowance = kSayBurst;
    const float since = std::chrono::duration<float>(now - one.saidAt).count();
    one.sayAllowance = std::min(kSayBurst, one.sayAllowance + since / kSayEvery);
    one.saidAt = now;
    if (one.sayAllowance < 1.0f) return false;
    one.sayAllowance -= 1.0f;
    return true;
}

// One Say: to whom it goes, and as what each hears it.
void say(Session& one, const net::Say& said, std::vector<std::unique_ptr<Session>>& sessions) {
    const std::string text = net::saidText(said.text);
    if (text.empty() || one.player == 0) return;
    if (!sayAllowed(one)) {
        chatSystem(one, net::Channel::Server, "You are speaking too quickly.");
        return;
    }
    net::Heard line;
    line.channel = said.channel;
    line.name = one.name;
    line.text = text;
    line.bot = one.bot;
    // Who hears it, and the speaker's body where the hearer can see him.
    const auto tell = [&](Session& hearer) {
        line.from = hearer.world == one.world ? one.player : 0;
        hearLine(hearer, line);
    };
    switch (said.channel) {
        case net::Channel::Map:
            for (auto& other : sessions) {
                if (other->world == one.world) tell(*other);
            }
            core::logf("chat %s/%d %s: %s", one.world->name.c_str(), one.world->copy, one.name.c_str(),
                       text.c_str());
            break;
        case net::Channel::Party: {
            const std::vector<Session*> mates = partyMates(one, sessions);
            if (mates.size() < 2) {
                chatSystem(one, net::Channel::Party, "You are not in a party.");
                return;
            }
            for (Session* mate : mates) tell(*mate);
            core::logf("chat party %s: %s", one.name.c_str(), text.c_str());
            break;
        }
        case net::Channel::Guild:
            // No guilds yet (docs/server-plan.md phase 7).
            chatSystem(one, net::Channel::Guild, "You are not in a guild.");
            return;
        case net::Channel::Whisper: {
            Session* to = playerNamed(said.to, sessions);
            if (to == nullptr) {
                chatSystem(one, net::Channel::Whisper, net::saidText(said.to).substr(0, 16) + " is not here.");
                return;
            }
            if (to == &one) return;
            tell(*to);
            line.to = to->name;  // and his own copy back: "To Valeria"
            tell(one);
            core::logf("chat whisper %s > %s: %s", one.name.c_str(), to->name.c_str(), text.c_str());
            break;
        }
        case net::Channel::Server:
            return;  // the server's alone
    }
}

// ---- the party (docs/party.md): its asks and its upkeep ----------------------------------------

// The welcomed connection his character is on, or nullptr.
Session* sessionOfToken(uint64_t token, std::vector<std::unique_ptr<Session>>& sessions) {
    for (auto& one : sessions) {
        if (one->token == token && one->welcomed && one->socket.open() && one->world != nullptr) {
            return one.get();
        }
    }
    return nullptr;
}

// His realm told his party: the next tick of his world carries it.
void partySet(Session& one, uint32_t party) {
    if (!one.welcomed || one.world == nullptr || one.player == 0) return;
    // Asked as his own, so every mirror finds the same asker: player 0 is whoever is first in
    // that realm, which a mirror and the server do not agree on (a divergence at the first hash).
    one.world->queued.push_back({.kind = sim::Command::Kind::PartySet, .player = one.player,
                                 .a = int(party), .target = one.player});
}

// A line on the Party channel to every member on a connection.
void partyLine(const Party& party, const std::string& text, std::vector<std::unique_ptr<Session>>& sessions) {
    for (const Party::Member& m : party.members) {
        if (Session* one = sessionOfToken(m.token, sessions)) chatSystem(*one, net::Channel::Party, text);
    }
}

void sendPartyState(Session& to, const net::PartyState& state) {
    if (!to.welcomed || !to.socket.open()) return;
    std::vector<uint8_t> out;
    net::put(out, state);
    if (!to.socket.send(out)) to.socket.close();
}

// The bar's state to every member: each member's level, class, map and health as his own realm
// has him, his body only to those in his world.
void sendParty(const Party& party, std::vector<std::unique_ptr<Session>>& sessions) {
    for (const Party::Member& to : party.members) {
        Session* hearer = sessionOfToken(to.token, sessions);
        if (hearer == nullptr) continue;
        net::PartyState state;
        for (const Party::Member& m : party.members) {
            net::PartyState::Member out;
            out.name = m.name;
            if (Session* one = sessionOfToken(m.token, sessions)) {
                out.world = one->world->name;
                out.bot = one->bot;
                out.here = one->world == hearer->world ? one->player : 0;
                if (const sim::Body* him = one->world->realm->find(one->player)) {
                    out.kin = uint8_t(him->kin);
                    out.level = him->level;
                    out.health = uint8_t(std::clamp(him->maxHealth > 0 ? 100 * him->health / him->maxHealth : 0, 0, 100));
                    out.mana = uint8_t(std::clamp(him->maxMana > 0 ? 100 * him->mana / him->maxMana : 0, 0, 100));
                    out.shield = him->maxSd > 0 ? uint8_t(std::clamp(100 * him->sd / him->maxSd, 0, 100))
                                                : net::PartyState::kNoShield;
                    out.life = him->health;
                    out.lifeMost = him->maxHealth;
                }
            }
            state.members.push_back(std::move(out));
        }
        sendPartyState(*hearer, state);
    }
}

// One out of his party: told so, his realm told, the rest told why. A party of one left is no
// party; a leader gone hands it to the next.
void partyOut(uint64_t token, const std::string& why, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto at = g_partyOf.find(token);
    if (at == g_partyOf.end()) return;
    Party& party = g_parties[at->second];
    g_partyOf.erase(at);
    g_partyBody.erase(token);
    g_partyAway.erase(token);
    std::string name;
    const bool leader = !party.members.empty() && party.members.front().token == token;
    for (size_t i = 0; i < party.members.size(); ++i) {
        if (party.members[i].token != token) continue;
        name = party.members[i].name;
        party.members.erase(party.members.begin() + long(i));
        break;
    }
    if (Session* one = sessionOfToken(token, sessions)) {
        partySet(*one, 0);
        sendPartyState(*one, {});
        chatSystem(*one, net::Channel::Party, why == "was kicked" ? "You were removed from the party." : "You left the party.");
    }
    partyLine(party, name + " " + (why.empty() ? "has left the party." : why + " from the party."), sessions);
    core::logf("party %u: %s %s", party.id, name.c_str(), why.empty() ? "left" : why.c_str());
    if (party.members.size() < 2) {
        for (const Party::Member& m : party.members) {
            g_partyOf.erase(m.token);
            g_partyBody.erase(m.token);
            g_partyAway.erase(m.token);
            if (Session* one = sessionOfToken(m.token, sessions)) {
                partySet(*one, 0);
                sendPartyState(*one, {});
                chatSystem(*one, net::Channel::Party, "The party has been disbanded.");
            }
        }
        g_parties.erase(party.id);
        return;
    }
    if (leader) partyLine(party, party.members.front().name + " is now the party leader.", sessions);
    sendParty(party, sessions);
}

// What he asked of the party (net::PartyAsk), answered on the Party channel.
void partyAsk(Session& one, const net::PartyAsk& ask, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto tell = [&](const std::string& text) { chatSystem(one, net::Channel::Party, text); };
    const auto now = Clock::now();
    const auto mine = g_partyOf.find(one.token);
    Party* party = mine != g_partyOf.end() ? &g_parties[mine->second] : nullptr;
    switch (ask.what) {
        case net::PartyWhat::Invite: {
            Session* them = playerNamed(ask.name, sessions);
            if (them == nullptr) return tell(ask.name + " is not online.");
            if (them == &one) return tell("You cannot invite yourself.");
            if (g_partyOf.count(them->token)) return tell(them->name + " is already in a party.");
            if (party != nullptr && party->members.front().token != one.token) {
                return tell("Only the party leader can invite.");
            }
            if (party != nullptr && int(party->members.size()) >= sim::kPartyMost) return tell("Your party is full.");
            g_invites[them->token] = {one.token, now};
            chatSystem(*them, net::Channel::Party, one.name + " invites you to a party. Type /accept or /decline.");
            tell("You invited " + them->name + " to the party.");
            return;
        }
        case net::PartyWhat::Accept: {
            const auto invite = g_invites.find(one.token);
            if (invite == g_invites.end() || now - invite->second.at > kInviteFor) {
                return tell("You have no party invitation.");
            }
            const uint64_t from = invite->second.from;
            Session* inviter = sessionOfToken(from, sessions);
            // "/accept NAME" takes NAME's invitation and no other: a second ask that came in
            // between put a bot waiting on his own party's leader into a stranger's.
            if (!ask.name.empty() && inviter != nullptr && playerNamed(ask.name, sessions) != inviter) {
                return tell("You have no party invitation from " + ask.name + ".");
            }
            g_invites.erase(invite);
            if (party != nullptr) return tell("You are already in a party.");
            if (inviter == nullptr) return tell("Whoever invited you is no longer online.");
            const auto theirs = g_partyOf.find(from);
            Party* joined = theirs != g_partyOf.end() ? &g_parties[theirs->second] : nullptr;
            // Asked by a leader who has since left his party or joined another's as a member:
            // his invitation was his party's, and is no longer his to give.
            if (joined != nullptr && joined->members.front().token != from) {
                return tell(inviter->name + " no longer leads a party.");
            }
            if (joined != nullptr && int(joined->members.size()) >= sim::kPartyMost) return tell("That party is full.");
            if (joined == nullptr) {
                Party made;
                made.id = g_nextParty++;
                made.members.push_back({from, inviter->name});
                joined = &(g_parties[made.id] = made);
                g_partyOf[from] = made.id;
                core::logf("party %u: made by %s", made.id, inviter->name.c_str());
            }
            joined->members.push_back({one.token, one.name});
            g_partyOf[one.token] = joined->id;
            core::logf("party %u: %s joins (%zu)", joined->id, one.name.c_str(), joined->members.size());
            // Their realms are told at the next upkeep (partyTick), which finds each body.
            partyLine(*joined, one.name + " has joined the party.", sessions);
            sendParty(*joined, sessions);
            return;
        }
        case net::PartyWhat::Decline: {
            const auto invite = g_invites.find(one.token);
            if (invite == g_invites.end()) return tell("You have no party invitation.");
            if (Session* inviter = sessionOfToken(invite->second.from, sessions)) {
                chatSystem(*inviter, net::Channel::Party, one.name + " declined your invitation.");
            }
            g_invites.erase(invite);
            return tell("You declined the invitation.");
        }
        case net::PartyWhat::Leave:
            if (party == nullptr) return tell("You are not in a party.");
            partyOut(one.token, "", sessions);
            return;
        case net::PartyWhat::Kick: {
            if (party == nullptr) return tell("You are not in a party.");
            if (party->members.front().token != one.token) return tell("Only the party leader can remove members.");
            for (const Party::Member& m : party->members) {
                std::string a = m.name, b = ask.name;
                for (char& c : a) c = char(std::tolower(uint8_t(c)));
                for (char& c : b) c = char(std::tolower(uint8_t(c)));
                if (a != b || m.token == one.token) continue;
                const uint64_t token = m.token;
                partyOut(token, "was kicked", sessions);
                return;
            }
            return tell(ask.name + " is not in your party.");
        }
        case net::PartyWhat::Promote: {
            if (party == nullptr) return tell("You are not in a party.");
            if (party->members.front().token != one.token) return tell("Only the party leader can choose another.");
            for (size_t i = 1; i < party->members.size(); ++i) {
                std::string a = party->members[i].name, b = ask.name;
                for (char& c : a) c = char(std::tolower(uint8_t(c)));
                for (char& c : b) c = char(std::tolower(uint8_t(c)));
                if (a != b) continue;
                // The leader is the first; the old one steps to second.
                std::swap(party->members[0], party->members[i]);
                core::logf("party %u: %s hands it to %s", party->id, one.name.c_str(), party->members[0].name.c_str());
                partyLine(*party, party->members.front().name + " is now the party leader.", sessions);
                sendParty(*party, sessions);
                return;
            }
            return tell(ask.name + " is not in your party.");
        }
    }
}

// Each second: every member whose body in a realm is new (he came in, or changed maps) put in
// his party there; a member on no connection for kPartyAway let go; and the bar's state to all.
void partyTick(std::vector<std::unique_ptr<Session>>& sessions) {
    const auto now = Clock::now();
    std::vector<uint64_t> gone;
    for (auto& [id, party] : g_parties) {
        for (const Party::Member& m : party.members) {
            Session* one = sessionOfToken(m.token, sessions);
            if (one == nullptr) {
                const auto away = g_partyAway.try_emplace(m.token, now).first;
                if (now - away->second > kPartyAway) gone.push_back(m.token);
                continue;
            }
            g_partyAway.erase(m.token);
            const std::pair<const World*, uint32_t> body{one->world, one->player};
            const auto told = g_partyBody.find(m.token);
            if (told == g_partyBody.end() || told->second != body) {
                partySet(*one, id);
                g_partyBody[m.token] = body;
            }
        }
    }
    for (const uint64_t token : gone) partyOut(token, "went away", sessions);
    for (auto& [id, party] : g_parties) sendParty(party, sessions);
    for (auto it = g_invites.begin(); it != g_invites.end();) {
        it = now - it->second.at > kInviteFor ? g_invites.erase(it) : std::next(it);
    }
}

// An account's key: letters and digits the client made (game/account.h), long enough not to be
// guessed and short enough to be a key.
bool keyShape(const std::string& key) {
    if (key.size() < 16 || key.size() > net::kMostKey) return false;
    for (char c : key) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

// Whether a token is being played on some connection now.
bool inPlay(const std::vector<std::unique_ptr<Session>>& sessions, uint64_t token) {
    for (const auto& other : sessions) {
        if (other->token == token && other->world != nullptr && other->socket.open()) return true;
    }
    return false;
}

// The account's characters as the screen stands them: his pedestal, his name, his class and level,
// promoted or not, the world he comes into, and what he wears. One never played is level 1 in his
// class's town with his class's weapon, as he will be made.
net::Roster rosterOf(const std::string& account, net::Refused refused) {
    net::Roster roster;
    roster.refused = refused;
    for (const server::Store::Listed& one : g_store.list(account)) {
        net::Seat seat;
        seat.token = one.token;
        seat.slot = one.slot;
        seat.name = one.name;
        seat.kin = uint8_t(one.kin);
        if (one.fresh) {
            seat.level = sim::kNewLevel;
            seat.world = sim::homeWorld(one.kin);
            const int32_t weapon = g_items.itemNamed(sim::cradleWeapon(one.kin));
            if (weapon >= 0) seat.worn.push_back({uint8_t(sim::kWeaponRight), sim::Held{weapon, 0, 1}});
        } else {
            const sim::HeroRecord& hero = one.kept.hero;
            seat.level = hero.level;
            seat.second = sim::promoted(hero.quests, int(hero.kin));
            seat.world = one.world.empty() ? sim::homeWorld(hero.kin) : one.world;
            for (int slot = 0; slot < sim::kWorn; ++slot) {
                if (!hero.slots[slot].empty()) seat.worn.push_back({uint8_t(slot), hero.slots[slot]});
            }
        }
        roster.seats.push_back(std::move(seat));
    }
    return roster;
}

bool answer(Session& one, net::Refused refused) {
    std::vector<uint8_t> out;
    net::put(out, rosterOf(one.account, refused));
    return one.socket.send(out);
}

// The first free pedestal of an account, or -1.
int freeSlot(const std::string& account) {
    bool taken[sim::kRosterSlots] = {};
    for (const server::Store::Listed& one : g_store.list(account)) {
        if (one.slot >= 0 && one.slot < sim::kRosterSlots) taken[one.slot] = true;
    }
    for (int s = 0; s < sim::kRosterSlots; ++s) {
        if (!taken[s]) return s;
    }
    return -1;
}

// An Account: the key taken, the machine's old characters claimed onto it, the Roster said.
bool account(Session& one, const net::Account& said) {
    if (said.version != net::kVersion) {
        core::logError("%s: version %u, this server speaks %u", one.who.c_str(), said.version, net::kVersion);
        return false;
    }
    if (!keyShape(said.key)) {
        core::logError("%s: an account key of no shape", one.who.c_str());
        return false;
    }
    one.account = said.key;
    // Characters this machine played here before accounts: put on it, under the names its saves
    // gave them, when they are nobody's -- a name already taken, or no pedestal free, leaves him.
    for (const net::Claim& claim : said.claims) {
        if (claim.token == 0 || !sim::goodName(claim.name) || g_store.named(claim.name)) continue;
        int slot = claim.slot;
        for (const server::Store::Listed& mine : g_store.list(one.account)) {
            if (mine.slot == slot) slot = -1;
        }
        if (slot < 0 || slot >= sim::kRosterSlots) slot = freeSlot(one.account);
        if (slot < 0) continue;
        if (g_store.claim(one.account, claim.token, claim.name, slot)) {
            core::logf("%s: %s claimed onto his account, slot %d", one.who.c_str(), claim.name.c_str(), slot);
        }
    }
    one.who += " (" + one.account.substr(0, 6) + ")";
    core::logf("%s: at the character screen, %zu character(s)", one.who.c_str(),
               g_store.list(one.account).size());
    return answer(one, net::Refused::None);
}

bool create(Session& one, const net::Create& asked, std::mt19937_64& seeds,
            const std::vector<std::unique_ptr<Session>>& sessions) {
    const auto mine = g_store.list(one.account);
    const sim::Kin kin = sim::Kin(std::min<int>(asked.kin, int(sim::Kin::MagicGladiator)));
    bool gladiator = false;
    for (const server::Store::Listed& m : mine) {
        gladiator = gladiator || (!m.fresh && m.kept.hero.level >= sim::kGladiatorLevel);
    }
    net::Refused refused = net::Refused::None;
    if (!sim::goodName(asked.name)) refused = net::Refused::BadName;
    else if (kin == sim::Kin::MagicGladiator && !gladiator) refused = net::Refused::BadName;
    // Taken by a character kept, or by one playing now: a bot's name is in no account's list.
    else if (g_store.named(asked.name) || nameInUse(asked.name, sessions, nullptr)) refused = net::Refused::Taken;
    const int slot = freeSlot(one.account);
    if (refused == net::Refused::None && slot < 0) refused = net::Refused::NoRoom;
    if (refused == net::Refused::None) {
        uint64_t token = 0;
        do token = seeds(); while (token == 0 || g_store.has(token));
        if (!g_store.create(token, one.account, asked.name, kin, slot, sim::homeWorld(kin))) {
            return false;
        }
        core::logf("%s: made %s, class %d, slot %d", one.who.c_str(), asked.name.c_str(), int(kin), slot);
    }
    return answer(one, refused);
}

bool drop(Session& one, const net::Delete& asked, const std::vector<std::unique_ptr<Session>>& sessions) {
    net::Refused refused = net::Refused::None;
    if (inPlay(sessions, asked.token)) {
        refused = net::Refused::Playing;
    } else if (!g_store.remove(one.account, asked.token)) {
        refused = net::Refused::NotYours;
    } else {
        core::logf("%s: deleted %016llx", one.who.c_str(), (unsigned long long)asked.token);
    }
    return answer(one, refused);
}

bool worldName(const std::string& name) {
    // A world's name is a folder under cooked/: letters only, so a Hello cannot name a path.
    if (name.empty()) return false;
    for (char c : name) {
        if (!(c >= 'a' && c <= 'z')) return false;
    }
    return true;
}

// The world's first player: its realm raised round him, as sprint 18 raised one per connection.
bool invadedElsewhere(const World& world, const std::vector<std::unique_ptr<World>>& worlds);

World* raiseWorld(std::vector<std::unique_ptr<World>>& worlds, const net::Hello& hello,
                  const std::string& assets, const sim::RealmConfig& config, uint64_t seed,
                  const std::string& who, const sim::Kept* kept, int castle, int square = 0,
                  int squareWait = 0) {
    auto world = std::make_unique<World>();
    world->name = hello.world;
    world->castle = castle;
    world->square = square;
    std::string error;
    const std::string path = assets + "/cooked/" + hello.world + "/" + hello.world + ".mur";
    if (!content::loadTables(path, world->tables, error)) {
        core::logError("%s: cannot load %s: %s", who.c_str(), path.c_str(), error.c_str());
        return nullptr;
    }
    const int level = std::clamp(kept ? kept->hero.level : hello.level, 1, sim::kMaximumLevel);
    const sim::Kin kin =
        kept ? kept->hero.kin : sim::Kin(std::min<int>(hello.kin, int(sim::Kin::MagicGladiator)));
    // The Welcome first, and the realm raised from it: every mirror raises its own from the same
    // frame by the same sim::raiseFrom, so the two cannot differ (docs/order-plan.md sprint C).
    net::Welcome& w = world->start;
    w.seed = seed;
    w.world = hello.world;
    w.kin = uint8_t(kin);
    w.level = level;
    w.column = hello.column;
    w.row = hello.row;
    w.weapon = kept ? std::string() : hello.weapon;
    w.shield = kept ? std::string() : hello.shield;
    w.config = config;
    w.kept = kept != nullptr;
    w.castle = castle;
    w.square = square;
    w.squareWait = squareWait;
    if (kept) w.first = *kept;
    world->realm = std::make_unique<sim::Realm>();
    if (!sim::raiseFrom(*world->realm, &world->tables, net::raiseOrder(w))) {
        core::logError("%s: %s would not raise", who.c_str(), hello.world.c_str());
        return nullptr;
    }
    core::logf("%s: raised %s, class %d level %d at %d,%d, seed %llu", who.c_str(),
               hello.world.c_str(), int(kin), level, hello.column, hello.row,
               (unsigned long long)seed);
    if (g_storm) {
        world->weather.wet = true;
        world->weather.left = 300.0f;
    }
    if (g_invasion && !invadedElsewhere(*world, worlds)) world->realm->invade();
    worlds.push_back(std::move(world));
    return worlds.back().get();
}

// His Welcome: the world's start, his id, and the world's past after it.
bool welcome(Session& one, uint32_t player) {
    net::Welcome w = one.world->start;
    w.you = player;
    w.backlog = uint32_t(one.world->log.size());
    if (!one.world->snapshot.empty() && one.world->packed.empty()) {
        net::pack(one.world->snapshot, one.world->packed);
    }
    w.snapshot = one.world->packed;
    w.token = one.token;
    w.udpKey = g_udp.open() ? one.udpKey : 0;
    std::vector<uint8_t> out;
    net::put(out, w);
    for (const net::Tick& t : one.world->log) net::put(out, t);
    one.player = player;
    one.joining = 0;
    one.keepAt = Clock::now() + g_tick * kKeepEvery;
    one.welcomed = one.socket.send(out);
    g_stats.streamBytes += out.size();
    core::logf("%s: welcomed into %s as #%u, a %zu KB snapshot (%zu KB packed) and %u ticks after it, %d here",
               one.who.c_str(), one.world->name.c_str(), player, one.world->snapshot.size() / 1024,
               w.snapshot.size() / 1024, w.backlog,
               one.world->realm->playersHere());
    return one.welcomed;
}

void part(Session& one);

// Who is in a world or on his way in.
int peopleIn(const World& world, const std::vector<std::unique_ptr<Session>>& sessions) {
    int n = 0;
    for (const auto& one : sessions) n += one->world == &world && one->socket.open();
    return n;
}

bool hello(Session& one, const net::Hello& asked, std::vector<std::unique_ptr<World>>& worlds,
           std::vector<std::unique_ptr<Session>>& sessions, const std::string& assets,
           const sim::RealmConfig& config, std::mt19937_64& seeds) {
    if (asked.version != net::kVersion) {
        core::logError("%s: version %u, this server speaks %u", one.who.c_str(), asked.version,
                       net::kVersion);
        return false;
    }
    if (!worldName(asked.world)) {
        core::logError("%s: no world called '%s'", one.who.c_str(), asked.world.c_str());
        return false;
    }
    // A character is in one place at a time. A connection of his that went this same poll is
    // let go first, so he comes back as it left him; one still open keeps him, and this one is
    // refused.
    if (asked.token != 0) {
        for (auto& other : sessions) {
            if (other.get() == &one || other->token != asked.token || other->world == nullptr) continue;
            // Open, but nothing heard on it for kSilentFor: a line that died without a close (a
            // dropped Wi-Fi), which the server would hold for as long as the kernel kept it.
            // Let go, and he comes back on this one -- refused, he read "no such world".
            if (other->socket.open() && Clock::now() - other->heardAt < kSilentFor) {
                core::logError("%s: his character is already playing (%s)", one.who.c_str(),
                               other->who.c_str());
                return false;
            }
            if (other->socket.open()) {
                core::logf("%s: takes over %s, silent %.0f s", one.who.c_str(), other->who.c_str(),
                           std::chrono::duration<double>(Clock::now() - other->heardAt).count());
                bye(*other, net::Why::Elsewhere);
            }
            part(*other);
            other->world = nullptr;
        }
    }
    // Whose he is: a character of an account is played only with its key, and an account's are
    // made on the character screen, never by a Hello (phase 6).
    server::Store::Who whose;
    const bool known = asked.token != 0 && g_store.who(asked.token, whose);
    if (known && (whose.deleted || whose.account != asked.account)) {
        core::logError("%s: %016llx is not his to play", one.who.c_str(), (unsigned long long)asked.token);
        return false;
    }
    if (!known && !asked.account.empty()) {
        core::logError("%s: no such character on his account", one.who.c_str());
        return false;
    }
    if (known && !whose.name.empty()) one.who += " (" + whose.name + ")";
    // His name over his head: the account's for its characters, the Hello's for one of none.
    one.bot = asked.account.empty();
    one.name = known && !whose.name.empty() ? whose.name
               : one.bot && sim::goodName(asked.name) ? asked.name
                                                      : std::string();
    // A character of no account and no name (a bench's `--play --server`) is still someone on
    // the chat: whispered to, invited, never taken for the server's own line.
    // One of no account takes no name a character keeps or someone plays under: two of one name
    // and whispers, invites and kicks went to whichever was found first.
    if (one.bot && !one.name.empty() && (g_store.named(one.name) || nameInUse(one.name, sessions, &one))) {
        core::logError("%s: the name %s is taken", one.who.c_str(), one.name.c_str());
        return false;
    }
    if (one.name.empty()) {
        static int strangers = 0;
        one.name = "Stranger" + std::to_string(++strangers);
    }
    // Made on the screen and never played: made now as a new character, under his own token and
    // in the class he was made in.
    const bool fresh = known && whose.fresh;
    // His character, when his token names one the store keeps. Anything else is a new
    // character, and a new token.
    sim::Kept kept;
    std::string keptIn;
    bool carried = asked.token != 0 && !fresh && g_store.find(asked.token, kept, keptIn);
    net::Hello said = asked;
    if (fresh) said.kin = uint8_t(whose.kin);
    int castle = 0, square = 0, squareWait = 0;
    if (carried) {
        one.token = said.token;
        core::logf("%s: his character comes back, level %d with %lld Zen", one.who.c_str(),
                   kept.hero.level, (long long)kept.hero.money);
    }
    // Sent somewhere by the realm a moment ago -- a gate, a trip, home: taken now, whether or not
    // this Hello goes there, so an old one never says where he is later (placeOf).
    std::optional<Landing> sent;
    if (const auto due = g_due.find(one.token); carried && due != g_due.end()) {
        sent = due->second;
        g_due.erase(due);
    }
    if (sent && sent->world == said.world) {
        // There, at the tile it chose, and into the castle his ticket opened.
        if (sent->column != said.column || sent->row != said.row) {
            core::logf("%s: asked for %d,%d; put down at %d,%d", one.who.c_str(), said.column,
                       said.row, sent->column, sent->row);
        }
        said.column = sent->column;
        said.row = sent->row;
        castle = sent->castle;
        square = sent->square;
        squareWait = sent->squareWait;
    } else if (carried) {
        // Where the store has him: the world he was in or was sent to, at its tile (placeOf).
        // Every way between worlds is the realm's to say (a gate, a trip, home, Go Back!), so a
        // Hello for any other world is told where he is, and comes again there.
        if (!keptIn.empty() && keptIn != said.world) {
            const net::Elsewhere there{keptIn, kept.hero.column, kept.hero.row};
            std::vector<uint8_t> out;
            net::put(out, there);
            one.socket.send(out);
            one.socket.flush();
            core::logf("%s: his character is in %s at %d,%d, not %s: sent there", one.who.c_str(),
                       keptIn.c_str(), there.column, there.row, said.world.c_str());
            return false;
        } else if (!keptIn.empty()) {
            if (kept.hero.column != said.column || kept.hero.row != said.row) {
                core::logf("%s: asked for %d,%d; put down at %d,%d", one.who.c_str(), said.column,
                           said.row, kept.hero.column, kept.hero.row);
            }
            said.column = kept.hero.column;
            said.row = kept.hero.row;
        }
    } else {
        if (fresh) {
            one.token = asked.token;
        } else {
            do one.token = seeds(); while (one.token == 0 || g_store.has(one.token));
        }
        // A new character is the rules', not the client's: level 1, his class's weapon, at his
        // class's town's spawn gate (sim/cradle.h). Of the Hello only the class is his to choose,
        // as in MU's character creation. A first world elsewhere is told where he is born.
        const sim::Kin kin = sim::Kin(std::min<int>(said.kin, int(sim::Kin::MagicGladiator)));
        const char* home = sim::homeWorld(kin);
        if (const sim::MapRow* born = sim::mapOf(home)) {
            if (said.world != home) {
                const net::Elsewhere there{home, born->arrive[0], born->arrive[1]};
                std::vector<uint8_t> out;
                net::put(out, there);
                one.socket.send(out);
                one.socket.flush();
                core::logf("%s: a new character is born in %s, not %s: sent there", one.who.c_str(),
                           home, said.world.c_str());
                return false;
            }
            said.column = born->arrive[0];
            said.row = born->arrive[1];
        }
        said.level = sim::kNewLevel;
        said.weapon = sim::cradleWeapon(kin);
        said.shield.clear();
        // --kit: made at the kit's level with its things, and let in as carried (a test).
        if (g_kitLevel > 0) {
            sim::Realm scratch;
            if (scratch.raise(&g_items, seeds(), said.column, said.row, kin, g_kitLevel)) {
                sim::outfit(scratch, said.weapon, said.shield);
                for (const auto& [stem, plus] : g_kitItems) {
                    const int32_t item = g_items.itemNamed(stem);
                    if (item < 0 || scratch.give(item, -1, plus) < 0) {
                        core::logError("--kit: %s not given", stem.c_str());
                    }
                }
                kept = scratch.keptOf(scratch.hero().id);
                kept.hero.column = said.column;
                kept.hero.row = said.row;
                carried = true;
                core::logf("%s: a new character from the kit, level %d", one.who.c_str(), g_kitLevel);
            }
        }
    }
    // The fullest copy of the map with room, so players are together while they fit; a new copy
    // when none has. A Blood Castle is its own world and is never copied.
    World* into = nullptr;
    int most = -1;
    int copies = 0;
    for (auto& world : worlds) {
        if (world->name != said.world || world->castle != castle || world->square != square) continue;
        // A square's run takes newcomers only while it waits for its fight: the next run is a
        // new world (WebZen lets no one in after the gate shuts).
        if (square > 0 && world->realm->squareRun().phase != sim::SquarePhase::Waiting) {
            // But one through Charon's gate while it was open, his line or his load slower than
            // its last seconds, comes into the run just begun rather than a square of his own --
            // raised for him alone, its gate's seconds long gone.
            const sim::SquareRun& run = world->realm->squareRun();
            const bool late = run.phase == sim::SquarePhase::Running &&
                              world->realm->tick() - run.startsAt <= int64_t(kSquareLate) * sim::kSquareTicksPerSecond;
            if (!late) continue;
        }
        // And a castle's the same: its run is set when its world is raised (Realm::Realm), so one
        // begun or done takes nobody, and the next period's entrants raise a new one. Joined
        // after its first run, every castle sent its entrants straight back to Devias.
        if (castle > 0 && world->realm->castleRun().phase != sim::CastlePhase::Waiting) continue;
        copies = std::max(copies, world->copy);
        const int here = peopleIn(*world, sessions);
        if ((castle > 0 || square > 0 || here < g_mapCap) && here > most) {
            most = here;
            into = world.get();
        }
    }
    for (auto& world : worlds) {
        if (world.get() != into) continue;
        // Into a world already running: a Join at the next tick's start, his hands by arm index
        // -- or, carried, all of him (Realm::carry) and no cradle.
        const content::Tables& tables = world->tables;
        const int32_t held = carried || said.weapon.empty() ? -1 : tables.armNamed(said.weapon);
        const int32_t worn = carried || said.shield.empty() ? -1 : tables.armNamed(said.shield);
        sim::Command join;
        join.kind = sim::Command::Kind::Join;
        join.ticket = world->nextTicket++;
        join.a = carried ? int(kept.hero.kin) : std::min<int>(said.kin, int(sim::Kin::MagicGladiator));
        join.b = std::clamp(carried ? kept.hero.level : said.level, 1, sim::kMaximumLevel);
        join.c = said.column;
        join.d = said.row;
        join.target = uint32_t(held + 1);
        join.zen = worn + 1;
        world->queued.push_back(join);
        if (carried) world->arriving.push_back({join.ticket, kept});
        one.world = world.get();
        one.joining = join.ticket;
        core::logf("%s: joining %s, class %d level %d at %d,%d", one.who.c_str(),
                   said.world.c_str(), join.a, join.b, said.column, said.row);
        return true;
    }
    World* world = raiseWorld(worlds, said, assets, config, seeds(), one.who, carried ? &kept : nullptr,
                              castle, square, squareWait);
    if (world == nullptr) return false;
    if (copies > 0) {
        world->copy = copies + 1;
        world->start.copy = uint8_t(std::min(world->copy, 255));
        core::logf("%s: %s is full, its copy %d raised", one.who.c_str(), said.world.c_str(), world->copy);
    }
    one.world = world;
    if (!welcome(one, world->realm->hero().id)) return false;
    announce(*world, sessions);
    return true;
}

// Everything that arrived on one connection. False when it must go.
bool hear(Session& one, std::vector<std::unique_ptr<World>>& worlds,
          std::vector<std::unique_ptr<Session>>& sessions, const std::string& assets,
          const sim::RealmConfig& config, std::mt19937_64& seeds) {
    const size_t had = one.in.size();
    if (!one.socket.receive(one.in)) return false;
    if (one.in.size() != had) one.heardAt = Clock::now();
    net::Kind kind{};
    std::vector<uint8_t> body;
    while (true) {
        const int took = net::take(one.in, kind, body, net::kMostAsked);
        if (took == 0) return true;
        if (took < 0) {
            core::logError("%s: not our protocol", one.who.c_str());
            return false;
        }
        if (kind == net::Kind::Account && one.world == nullptr && one.account.empty()) {
            net::Account said;
            if (!net::parse(body, said) || !account(one, said)) return false;
        } else if (kind == net::Kind::Create && one.world == nullptr && !one.account.empty()) {
            net::Create asked;
            if (!net::parse(body, asked) || !create(one, asked, seeds, sessions)) return false;
        } else if (kind == net::Kind::Delete && one.world == nullptr && !one.account.empty()) {
            net::Delete asked;
            if (!net::parse(body, asked) || !drop(one, asked, sessions)) return false;
        } else if (kind == net::Kind::Hello && one.world == nullptr) {
            net::Hello said;
            if (!net::parse(body, said) || !hello(one, said, worlds, sessions, assets, config, seeds)) {
                return false;
            }
        } else if (kind == net::Kind::Command && one.welcomed) {
            sim::Command command;
            if (!net::parse(body, command)) return false;
            // The world's door is the server's own.
            if (command.kind == sim::Command::Kind::Join || command.kind == sim::Command::Kind::Leave ||
                command.kind == sim::Command::Kind::PartySet) {
                continue;
            }
            // Sent to another world already (sentAway): nothing more of his here. A client behind
            // the server asked a paid trip again before it saw the first one's answer, and each
            // was paid -- a net bot 25 s behind at --speed 100 paid 3000 Zen every 10 s, 952
            // times in 18 game hours (the Claude bot's runs, 2026-10-09). A player on a slow
            // line who clicks twice is the same.
            if (one.sentAway) continue;
            // Nothing asked is nothing to send everyone; and no more than kMostCommands a tick.
            if (command.kind == sim::Command::Kind::None || ++one.asked > kMostCommands) continue;
            // Whatever the client wrote there, it is his own hero who asks.
            command.player = one.player;
            // One trip a tick: the landing is read from the Travel its answer's ticket names
            // (landingOf), and a refused trip's ticket on a second, granted one landed him where
            // the first asked -- Lost Tower 7 at Noria's price, no level, quest or wings asked.
            if (command.kind == sim::Command::Kind::Travel &&
                std::any_of(one.world->queued.begin(), one.world->queued.end(), [&](const sim::Command& c) {
                    return c.kind == sim::Command::Kind::Travel && c.player == one.player;
                })) {
                continue;
            }
            if (command.kind == sim::Command::Kind::Order) {
                const sim::Body* him = one.world->realm->find(one.player);
                if (him != nullptr && !him->walking) one.world->wantsEarly = true;
            }
            one.world->queued.push_back(command);
        } else if (kind == net::Kind::PartyAsk && one.welcomed) {
            net::PartyAsk asked;
            if (!net::parse(body, asked)) return false;
            partyAsk(one, asked, sessions);
        } else if (kind == net::Kind::Chat && one.welcomed) {
            net::Say said;
            if (!net::parse(body, said)) return false;
            say(one, said, sessions);
        } else if (kind == net::Kind::Ping) {
            // Back the moment it is read, ahead of any tick: the line's round trip alone.
            net::Ping ping;
            if (!net::parse(body, ping)) return false;
            std::vector<uint8_t> out;
            net::put(out, ping);
            if (!one.socket.send(out)) return false;
        } else if ((kind == net::Kind::Command || kind == net::Kind::Chat || kind == net::Kind::PartyAsk) &&
                   one.joining != 0) {
            // Asked or said before his Welcome: he has no body yet to ask with, and nobody hears
            // him yet. Not a frame out of turn: a client quick on the keys after its Hello.
            continue;
        } else {
            core::logError("%s: a frame of kind %d out of turn", one.who.c_str(), int(kind));
            return false;
        }
    }
}

// Where the realm sends one of its players with what it said, when it is another world: through a
// gate (Gated: the exit gate's map, at the tile the realm chose), by Tab's list (an answered
// Travel: the row's map and tile), or home from a map with no safe zone (a Town Portal read or a
// fall from flight, Warped; a death, Rose; both with c 1) to the map row's town, at its spawn
// gate. The same tables the client changes its map by (sim/maps.h, gates.h, travel.h).
std::optional<Landing> landingOf(const World& world, const sim::Happening& said,
                                 const std::vector<sim::Command>& commands) {
    const auto home = [&]() -> std::optional<Landing> {
        const sim::MapRow* here = sim::mapOf(world.name);
        const sim::MapRow* town = sim::mapNumbered(here ? here->home : 0);
        if (town == nullptr) return std::nullopt;
        return Landing{town->world, town->arrive[0], town->arrive[1]};
    };
    switch (said.what) {
        case sim::What::Gated: {
            const sim::EnterGate* in = sim::enterGateNumbered(said.a);
            const sim::ExitGate* out = in ? sim::exitGate(in->target) : nullptr;
            const sim::MapRow* map = out ? sim::mapNumbered(int(out->map)) : nullptr;
            if (map == nullptr) return std::nullopt;
            const int castle = said.a == sim::kCastleEnterGate ? world.realm->castlePassedOf(said.who) : 0;
            Landing landing{map->world, said.b, said.c, castle};
            // And Charon's: which square, and what was left of his gate as he went.
            if (sim::squareEnterGateNumbered(said.a)) {
                landing.square = world.realm->squarePassedOf(said.who);
                landing.squareWait = world.realm->squarePassedWaitOf(said.who);
            }
            return landing;
        }
        case sim::What::Answered: {
            if (said.a != int32_t(sim::Command::Kind::Travel) || said.b <= 0) return std::nullopt;
            for (const sim::Command& asked : commands) {
                if (asked.kind != sim::Command::Kind::Travel || asked.player != said.who ||
                    asked.ticket != uint32_t(said.c) || asked.a < 0 || asked.a >= sim::kTravels) {
                    continue;
                }
                const sim::TravelRow& to = sim::travelAt(asked.a);
                // A floor of this same map: the realm has set him down there already.
                if (to.map == int32_t(world.tables.map)) return std::nullopt;
                const sim::MapRow* map = sim::mapNumbered(to.map);
                if (map == nullptr) return std::nullopt;
                return Landing{map->world, to.column, to.row, 0, true};
            }
            return std::nullopt;
        }
        case sim::What::WentBack: {
            const sim::MapRow* map = sim::mapNumbered(said.a);
            if (map == nullptr) return std::nullopt;
            return Landing{map->world, said.b, said.c};
        }
        case sim::What::Warped:
        case sim::What::Rose:
            return said.c == 1 ? home() : std::nullopt;
        default:
            return std::nullopt;
    }
}

// The welcomed connection playing body `id` in `world`, or nullptr.
Session* playing(const World& world, std::vector<std::unique_ptr<Session>>& sessions, uint32_t id) {
    for (auto& one : sessions) {
        if (one->world == &world && one->welcomed && one->player == id) return one.get();
    }
    return nullptr;
}

// One tick of one world: its inputs applied, the step, sent to everyone in it with now and then
// its hash; then whoever it let in, welcomed.
// Whether the Golden Dragon is in the sky or on the ground of any world but `world`: one at a
// time on the server (the user, 2026-10-08: "if there is multiple storms on maps, that only on 1
// storm he will land not multiple").
bool invadedElsewhere(const World& world, const std::vector<std::unique_ptr<World>>& worlds) {
    for (const auto& other : worlds) {
        if (other.get() != &world && other->realm &&
            other->realm->invasionPhase() != sim::InvasionPhase::Quiet) {
            return true;
        }
    }
    return false;
}

// A snapshot packed on its worker, put in hand: the past it stands after let go.
void takePacked(World& world) {
    if (!world.packing.valid() ||
        world.packing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    World::Packed done = world.packing.get();
    if (!done.snapshot.empty() && done.packed.empty()) return;  // zlib said no: the last one stands
    world.snapshot = std::move(done.snapshot);
    world.packed = std::move(done.packed);
    world.log.erase(world.log.begin(), std::find_if(world.log.begin(), world.log.end(), [&](const net::Tick& t) {
                        return t.tick > done.tick;
                    }));
    ++g_stats.packs;
}

void tick(World& world, std::vector<std::unique_ptr<Session>>& sessions,
          const std::vector<std::unique_ptr<World>>& worlds, bool early = false) {
    net::Tick t;
    t.early = early;
    t.wallClock = wallNow();
    world.weather.tick(float(kTickSeconds),
                       world.realm->invasionPhase() != sim::InvasionPhase::Quiet);
    t.rain = world.weather.wet;
    t.invasionElsewhere = invadedElsewhere(world, worlds);
    t.commands.swap(world.queued);
    for (auto& one : sessions) {
        if (one->world == &world) one->asked = 0;
    }
    t.arrivals.swap(world.arriving);
    sim::Realm& realm = *world.realm;
    realm.setWallClock(t.wallClock);
    realm.invasionRain(t.rain, t.invasionElsewhere);
    for (const net::Arrival& a : t.arrivals) realm.carry(a.ticket, a.kept);
    for (const sim::Command& c : t.commands) realm.command(c);
    const bool castleOut = realm.castleRun().sentOut;
    const bool invaded = realm.invasionPhase() != sim::InvasionPhase::Quiet;
    realm.step();
    // The dragon killed or flown away: its storm goes with it, the next tick dry (the user,
    // 2026-10-08: "go kill dragon and weather clears").
    if (invaded && realm.invasionPhase() == sim::InvasionPhase::Quiet && world.weather.wet) {
        world.weather.left = 0.0f;
    }
    t.tick = uint32_t(realm.tick());
    // Whoever the realm sent to another world: due there.
    for (const sim::Happening& said : realm.happenings()) {
        if (const std::optional<Landing> landing = landingOf(world, said, t.commands)) {
            if (Session* one = playing(world, sessions, said.who)) {
                g_due[one->token] = *landing;
                one->sentAway = true;
                core::logf("%s: sent to %s %d,%d", one->who.c_str(), landing->world.c_str(),
                           landing->column, landing->row);
            }
        }
    }
    // A trade made: both characters written at once, as WebZen saves both after the swap
    // (user.cpp:19540-19554), so a crash a minute later cannot hand the things back to both.
    for (const sim::Happening& said : realm.happenings()) {
        if (said.what != sim::What::Deal || said.a != int32_t(sim::DealEvent::Done)) continue;
        if (Session* one = playing(world, sessions, said.who)) one->keepAt = Clock::time_point{};
    }
    // Blood Castle's run over and its rest out: its player to the castle's town. The run is the
    // first player's (a known limit of sprint 19).
    if (!castleOut && realm.castleRun().sentOut && realm.playerCount() > 0) {
        const sim::MapRow* here = sim::mapOf(world.name);
        const sim::MapRow* town = sim::mapNumbered(here ? here->home : 0);
        Session* one = playing(world, sessions, realm.playerAt(0).id);
        if (town != nullptr && one != nullptr) {
            g_due[one->token] = {town->world, town->arrive[0], town->arrive[1]};
            one->sentAway = true;
            core::logf("%s: out of the castle, to %s", one->who.c_str(), town->world);
        }
    }
    std::vector<uint8_t> out;
    net::put(out, t);
    // The tick's own frame, before the hash goes on: the datagram's.
    world.recent.push_back(out);
    while (world.recent.size() > kRecentTicks) world.recent.pop_front();
    if (t.tick % kHashEvery == 0) net::put(out, net::Hash{t.tick, net::stateHash(realm)});
    world.log.push_back(std::move(t));
    std::vector<uint8_t> datagram;
    if (g_udp.open()) {
        std::vector<const std::vector<uint8_t>*> frames;
        for (const auto& frame : world.recent) frames.push_back(&frame);
        if (!net::putTicks(datagram, frames)) datagram.clear();  // too big a tick: the stream's alone
    }
    for (auto& one : sessions) {
        if (one->world != &world || !one->welcomed || !one->socket.open()) continue;
        if (!one->socket.send(out)) one->socket.close();
        g_stats.streamBytes += out.size();
        if (one->udpKnown && !datagram.empty()) {
            g_udp.send(datagram.data(), datagram.size(), &one->udpAt);
            g_stats.udpBytes += datagram.size();
        }
    }
    // The world as it stands after this tick, taken once the tick is on its way (sprint 25), and
    // packed on a worker; the past before it is let go when the pack is in (takePacked). A realm
    // a snapshot cannot carry (a raid's) keeps its whole past, as before.
    if (++world.sinceSnapshot >= kSnapshotEvery && !world.packing.valid()) {
        world.sinceSnapshot = 0;
        std::vector<uint8_t> now;
        const Clock::time_point at = Clock::now();
        if (realm.snapshot(now)) {
            g_stats.snapshotMost = std::max(g_stats.snapshotMost, msSince(at));
            world.packing = std::async(std::launch::async, [bytes = std::move(now), stands = uint32_t(realm.tick())]() mutable {
#ifdef __linux__
                setpriority(PRIO_PROCESS, 0, 10);  // this thread alone: the ticks come first
#endif
                World::Packed done{std::move(bytes), {}, stands};
                net::pack(done.snapshot, done.packed);
                return done;
            });
        }
    }
    // The Joins this tick answered: each newcomer welcomed with the past up to this tick, or,
    // with his connection already gone, sent out again.
    for (const sim::Happening& said : realm.happenings()) {
        if (said.what != sim::What::Answered || said.a != int32_t(sim::Command::Kind::Join)) continue;
        const uint32_t ticket = uint32_t(said.c);
        const auto orphan = std::find(world.orphans.begin(), world.orphans.end(), ticket);
        if (orphan != world.orphans.end()) {
            world.orphans.erase(orphan);
            if (said.b > 0) {
                world.queued.push_back({.kind = sim::Command::Kind::Leave, .player = uint32_t(said.b)});
            }
            continue;
        }
        for (auto& one : sessions) {
            if (one->world != &world || one->joining != ticket) continue;
            if (said.b <= 0) {
                core::logError("%s: nowhere to stand in %s", one->who.c_str(), world.name.c_str());
                one->socket.close();
            } else if (!welcome(*one, uint32_t(said.b))) {
                one->socket.close();
            } else {
                announce(world, sessions);
            }
        }
    }
}

// Where he is, to be written: this world at the tile he stands on, or the world the realm has
// sent him to at its tile. Never inside an event: a Blood Castle left mid-run comes back in its
// town, at the spawn gate, as WebZen logs him in (user.cpp:3147-3150).
server::Store::Row placeOf(const Session& one) {
    server::Store::Row row{one.token, one.world->name, one.world->realm->keptOf(one.player)};
    if (const auto due = g_due.find(one.token); due != g_due.end()) {
        row.world = due->second.world;
        row.kept.hero.column = due->second.column;
        row.kept.hero.row = due->second.row;
        // And without her summon: a gate dismisses it (realm_summon.cpp). A Tab trip carries it.
        if (!due->second.summon) row.kept.hero.summonSkill = 0;
    }
    const sim::MapRow* map = sim::mapOf(row.world);
    if (map != nullptr && map->event) {
        if (const sim::MapRow* town = sim::mapNumbered(map->home)) {
            row.world = town->world;
            row.kept.hero.column = town->arrive[0];
            row.kept.hero.row = town->arrive[1];
        }
    }
    return row;
}

// A connection gone: his character written under his token, and his player leaves the world at
// its next tick.
void part(Session& one) {
    if (one.world == nullptr) return;
    // What he asked since the last tick goes with him: no mirror has seen it, and run after his
    // character is written it was done twice -- a drop or a trade's accept a tick behind the
    // save, the thing in another's hands and still in his own when he came back.
    if (one.player != 0) {
        auto& queued = one.world->queued;
        queued.erase(std::remove_if(queued.begin(), queued.end(),
                                    [&](const sim::Command& c) { return c.player == one.player; }),
                     queued.end());
    }
    if (!one.welcomed && one.player != 0) {
        // His Join answered but its Welcome never reached him (the line went with the tick that
        // answered it): his body is in the world all the same, and leaves it. Without this it
        // stood there for good, counted among the players.
        one.world->queued.push_back({.kind = sim::Command::Kind::Leave, .player = one.player});
    } else if (one.welcomed) {
        const server::Store::Row row = placeOf(one);
        if (g_store.keep(row)) {
            core::logf("%s: his character kept in %s at %d,%d, level %d with %lld Zen", one.who.c_str(),
                       row.world.c_str(), row.kept.hero.column, row.kept.hero.row,
                       row.kept.hero.level, (long long)row.kept.hero.money);
        }
        one.world->queued.push_back({.kind = sim::Command::Kind::Leave, .player = one.player});
    } else if (one.joining != 0) {
        one.world->orphans.push_back(one.joining);
    }
}

// Everyone in a world, written in one go: every minute, and as the server stops.
void keepEveryone(std::vector<std::unique_ptr<Session>>& sessions) {
    std::vector<server::Store::Row> all;
    for (auto& one : sessions) {
        if (one->world != nullptr && one->welcomed) all.push_back(placeOf(*one));
    }
    if (!all.empty() && g_store.keep(all)) core::logf("characters: %zu kept", all.size());
}

}  // namespace

int main(int argc, char** argv) {
    int port = net::kDefaultPort;
    std::string assets = MU2_ASSET_DIR;
    std::string store = "characters.db";
    sim::RealmConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--assets" && i + 1 < argc) assets = argv[++i];
        else if (a == "--store" && i + 1 < argc) store = argv[++i];
        else if (a == "--speed" && i + 1 < argc) {
            g_speed = std::max(1, std::atoi(argv[++i]));
            g_tick /= g_speed;
        }
        else if (a == "--storm") g_storm = true;
        else if (a == "--invasion") g_invasion = true;
        else if (a == "--map-cap" && i + 1 < argc) g_mapCap = std::max(1, std::atoi(argv[++i]));
        else if (a == "--castle-period" && i + 1 < argc) {
            const int s = std::max(2, std::atoi(argv[++i]));
            config.castle = {s, s / 2, s / 2};
        } else if (a == "--kit" && i + 1 < argc) {
            // LEVEL:STEM[+N],STEM... -- every new character made so (a test).
            const std::string kit = argv[++i];
            const size_t colon = kit.find(':');
            g_kitLevel = std::clamp(std::atoi(kit.c_str()), 1, sim::kMaximumLevel);
            for (size_t at = colon == std::string::npos ? kit.size() : colon + 1; at < kit.size();) {
                size_t comma = kit.find(',', at);
                if (comma == std::string::npos) comma = kit.size();
                std::string one = kit.substr(at, comma - at);
                int plus = 0;
                if (const size_t p = one.find('+'); p != std::string::npos) {
                    plus = std::atoi(one.c_str() + p + 1);
                    one.resize(p);
                }
                if (!one.empty()) g_kitItems.push_back({one, plus});
                at = comma + 1;
            }
        } else if (a == "--square-period" && i + 1 < argc) {
            // Charon's gate every S seconds for S/2, not two-hourly (a test).
            const int s = std::max(2, std::atoi(argv[++i]));
            config.square = {s, s / 2, s / 2};
        } else {
            std::fprintf(stderr, "usage: mu2_server [--port N] [--assets DIR] [--store FILE] [--castle-period S] [--square-period S] [--kit LEVEL:STEM+N,...] [--storm] [--invasion] [--map-cap N] [--speed N]\n");
            return 2;
        }
    }
    // A line at a time: under systemd stdout is a pipe to the journal, fully buffered otherwise,
    // and who joined would not be seen until kilobytes later.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    std::string error;
    // A server that cannot keep its characters does not open: whatever was played on it would
    // be lost.
    if (!g_store.open(store, error)) {
        core::logError("%s", error.c_str());
        return 1;
    }
    // The roster's item rows (g_items): a server without them stands the screen's characters bare.
    if (!content::loadTables(assets + "/cooked/lorencia/lorencia.mur", g_items, error)) {
        core::logError("lorencia's tables: %s; the character screen's figures go bare", error.c_str());
    }
    net::Socket listener;
    if (!listener.listen(port, error)) {
        core::logError("%s", error.c_str());
        return 1;
    }
    if (!g_udp.bind(port, error)) core::logError("%s: the ticks go down the stream alone", error.c_str());
    core::logf("mu2_server: protocol %u, listening on %d%s, tables from %s, %d characters in %s",
               net::kVersion, port, g_udp.open() ? " (and udp)" : "", assets.c_str(), g_store.count(),
               store.c_str());

    // A token is all a character's login is until accounts: not one the clock would guess.
    std::random_device entropy;
    std::mt19937_64 seeds((uint64_t(entropy()) << 32) ^ entropy() ^ uint64_t(std::time(nullptr)));
    std::vector<std::unique_ptr<World>> worlds;
    std::vector<std::unique_ptr<Session>> sessions;
    // What the last wait said of each session, in its order then: whether it has something to
    // read (or has gone). One arrived since is read regardless.
    std::vector<bool> ready;
    std::vector<pollfd> watch;
    uint64_t arrivals = 0;
    while (!g_stop) {
        // Where this pass's time goes (g_stats): the poll's wait is not counted.
        const Clock::time_point passAt = Clock::now();
        const double passCpuAt = cpuMs();
        // Who arrived, and what each that spoke said.
        for (net::Socket s = listener.accept(); s.open(); s = listener.accept()) {
            auto one = std::make_unique<Session>();
            one->socket = std::move(s);
            one->who = "player " + std::to_string(++arrivals);
            do one->udpKey = seeds(); while (one->udpKey == 0);
            core::logf("%s: connected", one->who.c_str());
            sessions.push_back(std::move(one));
        }
        // Each UDP socket that says its key is where that player's datagrams go: said again a
        // second later, and from a new address when his router moves him.
        std::vector<uint8_t> datagram;
        net::Address from;
        while (g_udp.receive(datagram, &from)) {
            uint64_t key = 0;
            if (!net::takeBind(datagram, key)) continue;
            for (auto& one : sessions) {
                if (one->udpKey != key) continue;
                if (!one->udpKnown || !(one->udpAt == from)) {
                    core::logf("%s: his ticks by udp as well", one->who.c_str());
                }
                one->udpAt = from;
                one->udpKnown = true;
            }
        }
        for (size_t i = 0; i < sessions.size(); ++i) {
            Session& one = *sessions[i];
            if (i < ready.size() && !ready[i]) continue;
            if (one.socket.open() && !hear(one, worlds, sessions, assets, config, seeds)) bye(one, net::Why::Protocol);
        }
        const double heardMs = msSince(passAt);
        // Step every world on its deadline, owing ticks rather than dropping them, and flush.
        const auto now = Clock::now();
        const Clock::time_point ticksAt = now;
        // The slowest world's ticks this pass, for the slow-pass line.
        double worldMost = 0.0;
        std::string worldMostName;
        int stepped = 0;
        const auto timedTick = [&](World& world, bool early) {
            const double late = std::chrono::duration<double, std::milli>(now - world.next).count();
            if (!early && late > 0.0) {
                g_stats.lateMost = std::max(g_stats.lateMost, late);
                if (late >= std::chrono::duration<double, std::milli>(g_tick).count()) ++g_stats.behind;
            }
            const Clock::time_point at = Clock::now();
            tick(world, sessions, worlds, early);
            const double ms = msSince(at);
            ++g_stats.ticks;
            ++stepped;
            g_stats.tickMs += ms;
            const std::string name = world.name + (world.copy > 1 ? "/" + std::to_string(world.copy) : "");
            if (ms > g_stats.tickMost) {
                g_stats.tickMost = ms;
                g_stats.tickMostWorld = name;
            }
            if (ms > worldMost) {
                worldMost = ms;
                worldMostName = name;
            }
        };
        for (auto& world : worlds) {
            takePacked(*world);
            const bool early = world->wantsEarly && world->next - now > kEarlyWithin &&
                               now - world->earlyAt >= std::chrono::duration<double>(kEarlyApart);
            world->wantsEarly = false;
            if (early) {
                timedTick(*world, true);
                world->earlyAt = now;
                world->next = now + g_tick;
            }
        }
        // What each world owes, a tick of each in turn (sprint 25): after a stall one world's
        // twenty went back to back while every other world waited behind them.
        for (int owed = 0; owed < 20; ++owed) {
            bool any = false;
            for (auto& world : worlds) {
                if (world->next > now) continue;
                timedTick(*world, false);
                world->next += g_tick;
                any = true;
            }
            if (!any) break;
        }
        for (auto& world : worlds) {
            if (world->next < now) world->next = now;  // more than a second behind: start again
        }
        const double ticksMs = msSince(ticksAt);
        // The parties' upkeep, each second (partyTick).
        const Clock::time_point partyStarted = Clock::now();
        static Clock::time_point partyAt = now;
        if (now - partyAt >= std::chrono::seconds(1)) {
            partyTick(sessions);
            partyAt = now;
        }
        const double partyMs = msSince(partyStarted);
        // Whoever's minute it is, written in one go.
        const Clock::time_point savesAt = Clock::now();
        std::vector<server::Store::Row> due;
        for (auto& one : sessions) {
            if (one->world == nullptr || !one->welcomed || one->keepAt > now) continue;
            due.push_back(placeOf(*one));
            one->keepAt = now + g_tick * kKeepEvery;
        }
        if (!due.empty()) {
            g_store.keep(due);
            g_stats.saved += int(due.size());
        }
        const double savesMs = msSince(savesAt);
        g_stats.saveMost = std::max(g_stats.saveMost, savesMs);
        const Clock::time_point sendAt = Clock::now();
        for (auto& one : sessions) {
            if (!one->socket.open()) continue;
            if (!one->socket.flush()) {
                one->socket.close();
            } else if (one->socket.stalledSeconds() > kStalledSeconds || one->socket.waiting() > kMostOwed) {
                core::logError("%s: has taken nothing for %.0f s, %zu KB owed: let go", one->who.c_str(),
                               one->socket.stalledSeconds(), one->socket.waiting() / 1024);
                bye(*one, net::Why::Behind);
            }
        }
        const double sendMs = msSince(sendAt);
        const Clock::time_point goneAt = Clock::now();
        for (size_t i = 0; i < sessions.size();) {
            if (!sessions[i]->socket.open()) {
                core::logf("%s: gone", sessions[i]->who.c_str());
                part(*sessions[i]);
                sessions.erase(sessions.begin() + long(i));
            } else {
                ++i;
            }
        }
        // Worlds are never let go: the server is always online, and a world once raised keeps
        // its monsters, its weather and its state for as long as the server runs -- but for an
        // event's (a Blood Castle's, a Devil Square's run), which is one run's and nobody joins
        // again once it has begun: let go when nobody is in it, or every run left a world ticking.
        for (size_t i = 0; i < worlds.size();) {
            World& w = *worlds[i];
            if ((w.castle > 0 || w.square > 0) && peopleIn(w, sessions) == 0) {
                core::logf("%s: let go, nobody in it (castle %d, square %d)", w.name.c_str(), w.castle,
                           w.square);
                for (auto it = g_partyBody.begin(); it != g_partyBody.end();) {
                    it = it->second.first == &w ? g_partyBody.erase(it) : std::next(it);
                }
                worlds.erase(worlds.begin() + long(i));
            } else {
                ++i;
            }
        }
        const double goneMs = msSince(goneAt);
        // The pass's time, and where it went when it was long.
        const double passMs = msSince(passAt);
        g_stats.passMost = std::max(g_stats.passMost, passMs);
        if (passMs > kSlowPass) {
            const double passCpu = cpuMs() - passCpuAt;
            ++g_stats.slowPasses;
            if (passCpu < passMs / 2) ++g_stats.slowWaited;
            core::logf("slow pass %.1f ms, %.1f on the cpu: heard %.1f, ticks %.1f (%d, the longest %s %.1f), party %.1f, "
                       "saves %.1f (%zu), sent %.1f, gone %.1f",
                       passMs, passCpu, heardMs, ticksMs, stepped, worldMostName.empty() ? "-" : worldMostName.c_str(),
                       worldMost, partyMs, savesMs, due.size(), sendMs, goneMs);
        }
        // Each minute, how the loop kept time.
        if (Clock::now() - g_stats.since >= kStatsEvery) {
            const double seconds = std::chrono::duration<double>(Clock::now() - g_stats.since).count();
            int players = 0, bots = 0;
            for (const auto& one : sessions) {
                if (!one->welcomed || !one->socket.open()) continue;
                ++players;
                bots += one->bot;
            }
            core::logf("timing: %d players (%d bots), %zu worlds | %d ticks, %.3f ms mean, %.2f most (%s) | "
                       "pass most %.1f ms, %d slow (%d held up) | late most %.1f ms, %d a tick behind | out %.1f KB/s stream, "
                       "%.1f KB/s udp | %d saved, %.2f ms most | snapshot %.2f ms most, %d packed",
                       players, bots, worlds.size(), g_stats.ticks,
                       g_stats.ticks > 0 ? g_stats.tickMs / g_stats.ticks : 0.0, g_stats.tickMost,
                       g_stats.tickMostWorld.empty() ? "-" : g_stats.tickMostWorld.c_str(), g_stats.passMost,
                       g_stats.slowPasses, g_stats.slowWaited, g_stats.lateMost, g_stats.behind, g_stats.streamBytes / 1024.0 / seconds,
                       g_stats.udpBytes / 1024.0 / seconds, g_stats.saved, g_stats.saveMost, g_stats.snapshotMost,
                       g_stats.packs);
            g_stats = Stats{};
        }
        // Then asleep until a line speaks or the next world's tick is due (sprint 24). It was a
        // 2 ms nap and a read of every line after it, 500 times a second whether anyone spoke:
        // ~0.5 us a line a pass, 13% of the box at 500 players, and up to 2 ms on an order
        // before it was even read. The order that sets a player off now takes its tick at once.
        auto wake = Clock::now() + std::chrono::milliseconds(100);
        for (const auto& world : worlds) wake = std::min(wake, world->next);
        watch.clear();
        watch.push_back({listener.fd(), POLLIN, 0});
        for (const auto& one : sessions) {
            const short wants = short(POLLIN | (one->socket.waiting() > 0 ? POLLOUT : 0));
            watch.push_back({one->socket.fd(), wants, 0});
        }
        if (g_udp.open()) watch.push_back({g_udp.fd(), POLLIN, 0});
        const auto left = std::max<Clock::duration>(Clock::duration::zero(), wake - Clock::now());
#ifdef __linux__
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(left).count();
        const timespec timeout{time_t(ns / 1000000000), long(ns % 1000000000)};
        ppoll(watch.data(), nfds_t(watch.size()), &timeout, nullptr);
#else
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(left).count();
        poll(watch.data(), nfds_t(watch.size()), int((us + 999) / 1000));
#endif
        ready.assign(sessions.size(), false);
        for (size_t i = 0; i < sessions.size(); ++i) ready[i] = watch[i + 1].revents != 0;
    }
    keepEveryone(sessions);
    for (auto& one : sessions) bye(*one, net::Why::Stopping);
    core::logf("mu2_server: stopped, %zu connected", sessions.size());
    return 0;
}
