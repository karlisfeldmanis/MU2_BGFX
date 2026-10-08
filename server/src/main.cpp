// mu2_server: MU2_BGFX's server (docs/sprints/18-the-wire.md, server/README.md).
//
//   mu2_server [--port N] [--assets DIR] [--store FILE] [--castle-period S]
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
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "content/tables.h"
#include "core/log.h"
#include "net/socket.h"
#include "net/wire.h"
#include "sim/cradle.h"
#include "sim/quests.h"
#include "sim/gates.h"
#include "sim/maps.h"
#include "sim/travel.h"
#include "sim/realm.h"
#include "store.h"

namespace {

using namespace mu;

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

using Clock = std::chrono::steady_clock;
constexpr double kTickSeconds = 0.05;  // 20 Hz, the realm's own (sim/realm.h)
const Clock::duration kTick = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kTickSeconds));
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
// most a minute (docs/sprints/21-the-snapshot.md). About half a MB a town, taken in a millisecond.
constexpr int kSnapshotEvery = 60 * 20;
// A Join's ticket, the server's own: high, so it is never one a client numbered.
constexpr uint32_t kJoinTickets = 0x80000000u;
// Whoever is in a world is written this often as well as when he leaves: OpenMU's rate, and what
// a crash of the server can cost him.
constexpr int kKeepEvery = 60 * 20;

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
};
std::map<uint64_t, Landing> g_due;
// Tests (2026-10-08): `--storm` raises every world in a wet spell, `--invasion` begins the Golden
// Invasion as a world is raised on a map that has one -- so a client joining mid-storm and
// mid-invasion can be tried at once rather than after a dry spell and the dice.
bool g_storm = false;
bool g_invasion = false;

struct World {
    std::string name;
    content::Tables tables;  // owned: the realm keeps a pointer to them
    std::unique_ptr<sim::Realm> realm;
    net::Welcome start;      // how it was raised: what every mirror raises from
    // The world as it last stood in a snapshot (Realm::snapshot), empty before the first, and
    // every tick since it: what a newcomer's mirror is laid with and then replays.
    std::vector<uint8_t> snapshot;
    std::vector<net::Tick> log;
    int sinceSnapshot = 0;
    std::vector<sim::Command> queued;  // arrived since the last tick, in order
    std::vector<net::Arrival> arriving;  // characters carried in with this tick's Joins
    uint32_t nextTicket = kJoinTickets;
    std::vector<uint32_t> orphans;  // Joins whose connection went before they were answered
    int castle = 0;  // Blood Castle's number, 0 for any other map: one world a castle
    // Its own deadline, so one world's early tick moves no other's clock.
    Clock::time_point next = Clock::now();
    Clock::time_point earlyAt{};  // when it last took a tick early
    bool wantsEarly = false;      // an order this poll that set a standing player off

    // ---- server-side weather ------------------------------------------------------------------
    struct ServerWeather {
        bool wet = false;
        float left = 600.0f;  // seconds left in the current spell, starting dry (kDryLow)
        uint32_t seed = 0x9E3779B9u;

        float random01() {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            return float(seed & 0xFFFFu) / 65535.0f;
        }
        // `held`: a Golden Invasion is in its sky or on its ground, and its storm does not pass
        // until the dragon is killed or flies away (sim/invasion.h).
        void tick(float dt, bool held = false) {
            left -= dt;
            if (held && wet) left = std::max(left, 1.0f);
            if (left > 0.0f) return;
            wet = !wet;
            left = wet ? 180.0f + random01() * (300.0f - 180.0f)   // kWetLow to kWetHigh
                       : 600.0f + random01() * (1080.0f - 600.0f); // kDryLow to kDryHigh
        }
    } weather;
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
};

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

bool create(Session& one, const net::Create& asked, std::mt19937_64& seeds) {
    const auto mine = g_store.list(one.account);
    const sim::Kin kin = sim::Kin(std::min<int>(asked.kin, int(sim::Kin::MagicGladiator)));
    bool gladiator = false;
    for (const server::Store::Listed& m : mine) {
        gladiator = gladiator || (!m.fresh && m.kept.hero.level >= sim::kGladiatorLevel);
    }
    net::Refused refused = net::Refused::None;
    if (!sim::goodName(asked.name)) refused = net::Refused::BadName;
    else if (kin == sim::Kin::MagicGladiator && !gladiator) refused = net::Refused::BadName;
    else if (g_store.named(asked.name)) refused = net::Refused::Taken;
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
                  const std::string& who, const sim::Kept* kept, int castle) {
    auto world = std::make_unique<World>();
    world->name = hello.world;
    world->castle = castle;
    std::string error;
    const std::string path = assets + "/cooked/" + hello.world + "/" + hello.world + ".mur";
    if (!content::loadTables(path, world->tables, error)) {
        core::logError("%s: cannot load %s: %s", who.c_str(), path.c_str(), error.c_str());
        return nullptr;
    }
    const int level = std::clamp(kept ? kept->hero.level : hello.level, 1, sim::kMaximumLevel);
    const sim::Kin kin =
        kept ? kept->hero.kin : sim::Kin(std::min<int>(hello.kin, int(sim::Kin::MagicGladiator)));
    world->realm = std::make_unique<sim::Realm>();
    world->realm->configure(config);
    if (!world->realm->raise(&world->tables, seed, hello.column, hello.row, kin, level)) {
        core::logError("%s: %s would not raise", who.c_str(), hello.world.c_str());
        return nullptr;
    }
    // Which Blood Castle, before anything else is laid on it: its monsters and its statue's die,
    // at the same moment as every mirror's (Play::open).
    if (castle > 0) world->realm->setCastle(castle);
    // A new character's cradle, or all of one come from another world: never both.
    if (kept) {
        world->realm->restoreKept(*kept);
    } else {
        sim::outfit(*world->realm, hello.weapon, hello.shield);
    }
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
    if (kept) w.first = *kept;
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
    w.snapshot = one.world->snapshot;
    w.token = one.token;
    std::vector<uint8_t> out;
    net::put(out, w);
    for (const net::Tick& t : one.world->log) net::put(out, t);
    one.player = player;
    one.joining = 0;
    one.welcomed = one.socket.send(out);
    core::logf("%s: welcomed into %s as #%u, a %zu KB snapshot and %u ticks after it, %d here",
               one.who.c_str(), one.world->name.c_str(), player, w.snapshot.size() / 1024, w.backlog,
               one.world->realm->playersHere());
    return one.welcomed;
}

void part(Session& one);

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
            if (other->socket.open()) {
                core::logError("%s: his character is already playing (%s)", one.who.c_str(),
                               other->who.c_str());
                return false;
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
    // Made on the screen and never played: made now as a new character, under his own token and
    // in the class he was made in.
    const bool fresh = known && whose.fresh;
    // His character, when his token names one the store keeps. Anything else is a new
    // character, and a new token.
    sim::Kept kept;
    std::string keptIn;
    const bool carried = asked.token != 0 && !fresh && g_store.find(asked.token, kept, keptIn);
    net::Hello said = asked;
    if (fresh) said.kin = uint8_t(whose.kin);
    int castle = 0;
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
    }
    for (auto& world : worlds) {
        if (world->name != said.world || world->castle != castle) continue;
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
                              castle);
    if (world == nullptr) return false;
    one.world = world;
    if (!welcome(one, world->realm->hero().id)) return false;
    announce(*world, sessions);
    return true;
}

// Everything that arrived on one connection. False when it must go.
bool hear(Session& one, std::vector<std::unique_ptr<World>>& worlds,
          std::vector<std::unique_ptr<Session>>& sessions, const std::string& assets,
          const sim::RealmConfig& config, std::mt19937_64& seeds) {
    if (!one.socket.receive(one.in)) return false;
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
            if (!net::parse(body, asked) || !create(one, asked, seeds)) return false;
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
            if (command.kind == sim::Command::Kind::Join || command.kind == sim::Command::Kind::Leave) {
                continue;
            }
            // Whatever the client wrote there, it is his own hero who asks.
            command.player = one.player;
            if (command.kind == sim::Command::Kind::Order) {
                const sim::Body* him = one.world->realm->find(one.player);
                if (him != nullptr && !him->walking) one.world->wantsEarly = true;
            }
            one.world->queued.push_back(command);
        } else if (kind == net::Kind::Ping) {
            // Back the moment it is read, ahead of any tick: the line's round trip alone.
            net::Ping ping;
            if (!net::parse(body, ping)) return false;
            std::vector<uint8_t> out;
            net::put(out, ping);
            if (!one.socket.send(out)) return false;
        } else if (kind == net::Kind::Command && one.joining != 0) {
            // Asked before his Welcome: he has no body yet to ask with.
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
            return Landing{map->world, said.b, said.c, castle};
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
                return Landing{map->world, to.column, to.row};
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

void tick(World& world, std::vector<std::unique_ptr<Session>>& sessions,
          const std::vector<std::unique_ptr<World>>& worlds, bool early = false) {
    net::Tick t;
    t.early = early;
    t.wallClock = int64_t(std::time(nullptr));
    world.weather.tick(float(kTickSeconds),
                       world.realm->invasionPhase() != sim::InvasionPhase::Quiet);
    t.rain = world.weather.wet;
    t.invasionElsewhere = invadedElsewhere(world, worlds);
    t.commands.swap(world.queued);
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
                core::logf("%s: sent to %s %d,%d", one->who.c_str(), landing->world.c_str(),
                           landing->column, landing->row);
            }
        }
    }
    // Blood Castle's run over and its rest out: its player to the castle's town. The run is the
    // first player's (a known limit of sprint 19).
    if (!castleOut && realm.castleRun().sentOut && realm.playerCount() > 0) {
        const sim::MapRow* here = sim::mapOf(world.name);
        const sim::MapRow* town = sim::mapNumbered(here ? here->home : 0);
        Session* one = playing(world, sessions, realm.playerAt(0).id);
        if (town != nullptr && one != nullptr) {
            g_due[one->token] = {town->world, town->arrive[0], town->arrive[1]};
            core::logf("%s: out of the castle, to %s", one->who.c_str(), town->world);
        }
    }
    std::vector<uint8_t> out;
    net::put(out, t);
    if (t.tick % kHashEvery == 0) net::put(out, net::Hash{t.tick, net::stateHash(realm)});
    world.log.push_back(std::move(t));
    // The world as it stands after this tick, and the past before it let go. A realm a snapshot
    // cannot carry (a raid's) keeps its whole past, as before.
    if (++world.sinceSnapshot >= kSnapshotEvery) {
        world.sinceSnapshot = 0;
        std::vector<uint8_t> now;
        if (realm.snapshot(now)) {
            world.snapshot.swap(now);
            world.log.clear();
        }
    }
    for (auto& one : sessions) {
        if (one->world != &world || !one->welcomed || !one->socket.open()) continue;
        if (!one->socket.send(out)) one->socket.close();
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
        // And without her summon: a map change dismisses it (realm_summon.cpp).
        row.kept.hero.summonSkill = 0;
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
    if (one.welcomed) {
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
        else if (a == "--storm") g_storm = true;
        else if (a == "--invasion") g_invasion = true;
        else if (a == "--castle-period" && i + 1 < argc) {
            const int s = std::max(2, std::atoi(argv[++i]));
            config.castle = {s, s / 2, s / 2};
        } else {
            std::fprintf(stderr, "usage: mu2_server [--port N] [--assets DIR] [--store FILE] [--castle-period S] [--storm] [--invasion]\n");
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
    core::logf("mu2_server: protocol %u, listening on %d, tables from %s, %d characters in %s",
               net::kVersion, port, assets.c_str(), g_store.count(), store.c_str());

    // A token is all a character's login is until accounts: not one the clock would guess.
    std::random_device entropy;
    std::mt19937_64 seeds((uint64_t(entropy()) << 32) ^ entropy() ^ uint64_t(std::time(nullptr)));
    auto keepAt = Clock::now() + kTick * kKeepEvery;
    std::vector<std::unique_ptr<World>> worlds;
    std::vector<std::unique_ptr<Session>> sessions;
    uint64_t arrivals = 0;
    while (!g_stop) {
        // Poll: who arrived, what each said.
        for (net::Socket s = listener.accept(); s.open(); s = listener.accept()) {
            auto one = std::make_unique<Session>();
            one->socket = std::move(s);
            one->who = "player " + std::to_string(++arrivals);
            core::logf("%s: connected", one->who.c_str());
            sessions.push_back(std::move(one));
        }
        for (auto& one : sessions) {
            if (one->socket.open() && !hear(*one, worlds, sessions, assets, config, seeds)) one->socket.close();
        }
        // Step every world on its deadline, owing ticks rather than dropping them, and flush.
        const auto now = Clock::now();
        for (auto& world : worlds) {
            const bool early = world->wantsEarly && world->next - now > kEarlyWithin &&
                               now - world->earlyAt >= std::chrono::duration<double>(kEarlyApart);
            world->wantsEarly = false;
            if (early) {
                tick(*world, sessions, worlds, true);
                world->earlyAt = now;
                world->next = now + kTick;
                continue;
            }
            int owed = 0;
            while (world->next <= now && owed < 20) {
                tick(*world, sessions, worlds);
                world->next += kTick;
                ++owed;
            }
            if (world->next < now) world->next = now;  // more than a second behind: start again
        }
        if (keepAt <= now) {
            keepEveryone(sessions);
            keepAt = now + kTick * kKeepEvery;
        }
        for (auto& one : sessions) {
            if (one->socket.open() && !one->socket.flush()) one->socket.close();
        }
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
        // its monsters, its weather and its state for as long as the server runs.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    keepEveryone(sessions);
    core::logf("mu2_server: stopped, %zu connected", sessions.size());
    return 0;
}
