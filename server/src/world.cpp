// The world host: raising a world, a Hello and its Welcome, the tick, where a player lands
// next, and keeping characters. Split out of main.cpp (host.h).

#include "host.h"
#include "broker.h"
#include "economy.h"
#include "sim/wear.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>

namespace mu::server {

namespace {
// A world's cooked tables and their rules (net::rulesPrint), read once (readWorlds, at the start):
// what a Hello's must be and what the world is raised on. Read from disk when first wanted, a
// deploy staged under a running server (deploy.sh --stage writes out/assets/cooked) gave a world
// not yet raised the next build's tables, and its print refused every client of this one: back at
// the character screen at each login (Remora, Aida on Central Europe, 2026-10-22).
struct Read {
    content::Tables tables;
    uint64_t print = 0;
};
std::map<std::string, Read> g_read;

const Read* readOf(const std::string& assets, const std::string& world) {
    auto at = g_read.find(world);
    if (at == g_read.end()) {
        Read one;
        std::string error;
        const std::string path = assets + "/cooked/" + world + "/" + world + ".mur";
        if (content::loadTables(path, one.tables, error)) {
            one.print = net::rulesPrint(one.tables);
        } else {
            core::logError("cannot load %s: %s", path.c_str(), error.c_str());
        }
        at = g_read.emplace(world, std::move(one)).first;
    }
    return at->second.print != 0 ? &at->second : nullptr;
}

uint64_t rulesOf(const std::string& assets, const std::string& world) {
    const Read* read = readOf(assets, world);
    return read ? read->print : 0;
}

// A Hello of another build, told why before the line closes (net::Why::Outdated): RemoteLink::join
// says so, where a plain close reads as "wrong version, or no such world".
bool refuseOutdated(Session& one) {
    std::vector<uint8_t> out;
    net::put(out, net::Bye{net::Why::Outdated});
    if (one.socket.send(out)) one.socket.flush();
    return false;
}
}  // namespace


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
    std::vector<uint32_t> guilds;  // and the marks of those guilds, each once to each line
    for (const auto& one : sessions) {
        if (one->world == &world && one->welcomed && one->player != 0) {
            const uint32_t guild = guildOf(one->token);
            who.players.push_back({one->player, one->name, one->bot, guild});
            if (guild != 0 && std::find(guilds.begin(), guilds.end(), guild) == guilds.end()) guilds.push_back(guild);
        }
    }
    std::vector<uint8_t> out;
    net::put(out, who);
    for (const auto& one : sessions) {
        if (one->world != &world || !one->welcomed || !one->socket.open()) continue;
        sendMarks(*one, guilds);
        if (one->socket.open() && !one->socket.send(out)) one->socket.close();
    }
}

int readWorlds(const std::string& assets) {
    std::error_code failed;
    int n = 0;
    for (const auto& entry : std::filesystem::directory_iterator(assets + "/cooked", failed)) {
        // cooked/ holds more than worlds (wardrobe, figures, showing): a world has its own .mur.
        const std::string name = entry.path().filename().string();
        if (!entry.is_directory() || !worldName(name)) continue;
        if (!std::filesystem::exists(entry.path() / (name + ".mur"), failed)) continue;
        if (readOf(assets, name)) ++n;
    }
    return n;
}

bool worldName(const std::string& name) {
    // A world's name is a folder under cooked/: letters only, so a Hello cannot name a path.
    if (name.empty()) return false;
    for (char c : name) {
        if (!(c >= 'a' && c <= 'z')) return false;
    }
    return true;
}

World* raiseWorld(std::vector<std::unique_ptr<World>>& worlds, const net::Hello& hello,
                  const std::string& assets, const sim::RealmConfig& config, uint64_t seed,
                  const std::string& who, const sim::Kept* kept, int castle, int square,
                  int squareWait) {
    auto world = std::make_unique<World>();
    world->name = hello.world;
    world->castle = castle;
    world->square = square;
    const Read* read = readOf(assets, hello.world);
    if (!read) {
        core::logError("%s: no tables for %s", who.c_str(), hello.world.c_str());
        return nullptr;
    }
    world->tables = read->tables;
    const int level = std::clamp(kept ? kept->hero.level : hello.level, 1, sim::kMaximumLevel);
    const sim::Kin kin =
        kept ? kept->hero.kin : sim::Kin(std::min<int>(hello.kin, int(sim::Kin::MagicGladiator)));
    // The Welcome first, and the realm raised from it: every mirror raises its own from the same
    // frame by the same sim::raiseFrom, so the two cannot differ (docs/plans/order-plan.md sprint C).
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
    if (asked.items != g_items.itemsPrint()) {
        core::logError("%s: item list %016llx, this server's %016llx", one.who.c_str(),
                       (unsigned long long)asked.items, (unsigned long long)g_items.itemsPrint());
        return refuseOutdated(one);
    }
    if (!worldName(asked.world)) {
        core::logError("%s: no world called '%s'", one.who.c_str(), asked.world.c_str());
        return false;
    }
    // A mirror on other rules parts from the realm unseen (net::rulesPrint): a deploy that
    // changed them under a running game, its protocol the same, is the case.
    if (const uint64_t rules = rulesOf(assets, asked.world); asked.rules != rules) {
        core::logError("%s: rules %016llx for %s, this server's %016llx", one.who.c_str(),
                       (unsigned long long)asked.rules, asked.world.c_str(), (unsigned long long)rules);
        return refuseOutdated(one);
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
    if (known && leavingRealm(asked.token)) {
        core::logf("%s: %016llx is moving to another region", one.who.c_str(), (unsigned long long)asked.token);
        return false;
    }
    if (!known && !asked.account.empty()) {
        core::logError("%s: no such character on his account", one.who.c_str());
        return false;
    }
    // **One character of an account at a time**, as MU lets an account in once: the vault is the
    // account's (Store::vaultOf), and two of its characters in at once would each carry it, a
    // thing put in by one still there for the other. Another of his account's is let go, written
    // first (part), so this one's Hello reads the vault as it left it -- a character switch whose
    // old line has not closed yet takes over from it, and a second client's ends the first's.
    if (!asked.account.empty()) {
        for (auto& other : sessions) {
            if (other.get() == &one || other->world == nullptr || other->playedFor != asked.account ||
                other->token == asked.token) {
                continue;
            }
            if (other->socket.open()) {
                core::logf("%s: takes over his account from %s", one.who.c_str(), other->who.c_str());
                bye(*other, net::Why::Elsewhere);
            }
            part(*other);
            other->world = nullptr;
        }
    }
    if (known && !whose.name.empty()) one.who += " (" + whose.name + ")";
    // His name over his head: the account's for its characters, the Hello's for one of none.
    one.bot = asked.account.empty();
    one.playedFor = asked.account;
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
    one.newcomer = fresh && !g_store.anyPlayed(asked.account);
    int castle = 0, square = 0, squareWait = 0;
    // His account's vault, whichever of its characters he is (bug #34, 2026-10-20).
    sim::Vault vault;
    const bool banked = !asked.account.empty() && g_store.vaultOf(asked.account, vault);
    if (carried) {
        one.token = said.token;
        if (banked) kept.vault = vault;
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
                    if (stem == "Zen") {  // Zen+N: his purse
                        scratch.earn(plus);
                        continue;
                    }
                    const int32_t item = g_items.itemNamed(stem);
                    if (item < 0 || scratch.give(item, -1, plus) < 0) {
                        core::logError("--kit: %s not given", stem.c_str());
                    }
                }
                kept = scratch.keptOf(scratch.hero().id);
                for (int q : g_kitDone) {  // --kit-done: handed in once, resting
                    kept.hero.quests[size_t(q)] = sim::QuestProgress{};
                    kept.hero.quests[size_t(q)].state = sim::QuestState::Resting;
                    kept.hero.quests[size_t(q)].completions = 1;
                }
                kept.hero.column = said.column;
                kept.hero.row = said.row;
                carried = true;
                core::logf("%s: a new character from the kit, level %d", one.who.c_str(), g_kitLevel);
            }
        }
        // A new character of an account with something in its vault: made as the kit makes one,
        // at level 1 with his class's weapon, and let in as carried with the vault in his hands.
        bool something = banked && vault.zen() != 0;
        for (int cell = 0; banked && !something && cell < sim::kVaultCells; ++cell) something = !vault[cell].empty();
        if (something && !carried) {
            sim::Realm scratch;
            if (scratch.raise(&g_items, seeds(), said.column, said.row, kin, said.level)) {
                sim::outfit(scratch, said.weapon, said.shield);
                kept = scratch.keptOf(scratch.hero().id);
                kept.hero.column = said.column;
                kept.hero.row = said.row;
                carried = true;
            }
        }
        if (banked) kept.vault = vault;
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
    guildArrived(one, sessions);
    sayInvasion(one, invasionNow(worlds));
    welcomeNewcomer(one, sessions);
    return true;
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

void callDragon(std::vector<std::unique_ptr<World>>& worlds, const std::string& request) {
    std::string asked;
    if (std::FILE* f = std::fopen(request.c_str(), "r")) {
        char line[64] = {};
        if (std::fgets(line, sizeof line, f)) asked = line;
        std::fclose(f);
        std::remove(request.c_str());
        while (!asked.empty() && std::isspace(static_cast<unsigned char>(asked.back()))) asked.pop_back();
    }
    for (const auto& one : worlds) {
        if (one->realm && one->realm->invasionPhase() != sim::InvasionPhase::Quiet) {
            core::logf("dragon: called, but it is on %s already", one->name.c_str());
            return;
        }
    }
    const std::vector<std::string> order =
        asked.empty() ? std::vector<std::string>{"lorencia", "devias", "noria"} : std::vector<std::string>{asked};
    for (const std::string& name : order) {
        for (auto& one : worlds) {
            if (one->realm && one->name == name && sim::invasionMap(one->realm->tables()->map)) {
                one->dragonCalled = true;
                // Its storm, as a wet spell beginning: the dragons come in the rain (sim/invasion.h).
                if (!one->weather.wet) {
                    one->weather.wet = true;
                    one->weather.left = sim::kWetHigh;
                }
                core::logf("dragon: called on %s%s", name.c_str(),
                           one->copy > 1 ? (" copy " + std::to_string(one->copy)).c_str() : "");
                return;
            }
        }
    }
    core::logf("dragon: called on %s, but nobody is on a map it lands on", asked.empty() ? "any map" : asked.c_str());
}

// Where the Golden Dragon is on the server (net::Invasion): the one world whose invasion is not
// Quiet, its map, copy and the tile it comes down on; Quiet when there is none.
net::Invasion invasionNow(const std::vector<std::unique_ptr<World>>& worlds) {
    net::Invasion said;
    for (const auto& world : worlds) {
        if (!world->realm || world->realm->invasionPhase() == sim::InvasionPhase::Quiet) continue;
        const sim::Body* dragon = world->realm->invader();
        if (dragon == nullptr) continue;
        said.phase = world->realm->invasionPhase();
        said.map = world->realm->tables()->map;
        said.copy = uint8_t(std::clamp(world->copy, 1, 255));
        said.column = int16_t(dragon->homeColumn);
        said.row = int16_t(dragon->homeRow);
        break;
    }
    return said;
}

// It said to one welcomed line.
void sayInvasion(Session& to, const net::Invasion& said) {
    if (!to.welcomed || !to.socket.open()) return;
    std::vector<uint8_t> out;
    net::put(out, said);
    if (!to.socket.send(out)) to.socket.close();
}

// It said to every client on every map whenever it changes (a world ticked, or let go), so the
// big map and the travel list of a player elsewhere mark where it stands; and to each newcomer
// at his welcome (sayInvasion).
void invasionTick(std::vector<std::unique_ptr<Session>>& sessions,
                  const std::vector<std::unique_ptr<World>>& worlds) {
    static net::Invasion told;
    const net::Invasion now = invasionNow(worlds);
    if (now == told) return;
    told = now;
    core::logf("invasion: every client told phase %d on map %u copy %d at (%d, %d)", int(now.phase),
               now.map, int(now.copy), int(now.column), int(now.row));
    for (auto& one : sessions) sayInvasion(*one, now);
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

void sendDatagrams(World& world, std::vector<std::unique_ptr<Session>>& sessions) {
    if (!world.datagramDue) return;
    world.datagramDue = false;
    if (!g_udp.open()) return;
    std::vector<const std::vector<uint8_t>*> frames;
    for (const auto& frame : world.recent) frames.push_back(&frame);
    std::vector<uint8_t> datagram;
    if (!net::putTicks(datagram, frames) || datagram.empty()) return;  // too big a tick: the stream's alone
    for (auto& one : sessions) {
        if (one->world != &world || !one->welcomed || !one->socket.open() || !one->udpKnown) continue;
        g_udp.send(datagram.data(), datagram.size(), &one->udpAt);
        g_stats.udpBytes += datagram.size();
    }
}

void tick(World& world, std::vector<std::unique_ptr<Session>>& sessions,
          const std::vector<std::unique_ptr<World>>& worlds, bool early) {
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
    // Called by hand (callDragon): after the rain, as every mirror takes it (RemoteLink::step).
    t.invade = world.dragonCalled && !t.invasionElsewhere;
    world.dragonCalled = false;
    if (t.invade && !realm.invade()) t.invade = false;
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
    // A rebirth (sim/rebirth.h): written at once, as a trade is -- 10M Zen and 399 levels gone
    // must not come back after a crash -- and told on Global to everyone online.
    for (const sim::Happening& said : realm.happenings()) {
        if (said.what != sim::What::Reborn) continue;
        if (Session* one = playing(world, sessions, said.who)) {
            one->keepAt = Clock::time_point{};
            core::logf("%s: reborn, rebirth %d", one->who.c_str(), int(said.a));
            announceRebirth(one->name, int(said.a), sessions);
        }
    }
    // The market's commands answered: a listing up, a purchase paid (broker.h).
    brokerHeard(world, t.commands, realm.happenings(), sessions);
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
    // **Queued, and sent once a pass** (2026-10-13, 40 bots at --speed 100): a send and a
    // datagram a tick for every player was 85% of the server's time in the kernel, the realm 10%.
    // The pass flushes each line once after its ticks, and sends each world one datagram of its
    // last ticks (sendDatagrams); at --speed 1 a pass is one tick, as before.
    world.datagramDue = true;
    for (auto& one : sessions) {
        if (one->world != &world || !one->welcomed || !one->socket.open()) continue;
        one->socket.queue(out);
        g_stats.streamBytes += out.size();
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
                guildArrived(*one, sessions);
                sayInvasion(*one, invasionNow(worlds));
                welcomeNewcomer(*one, sessions);
            }
        }
    }
    // Where this tick's Zen came from and went (economy.h).
    economyTick(world, sessions);
    // Where the Golden Dragon is, to every client when it changed (net::Invasion).
    invasionTick(sessions, worlds);
}

bool runCutShort(const World& world) {
    if (world.castle > 0) {
        const sim::CastlePhase phase = world.realm->castleRun().phase;
        return phase == sim::CastlePhase::Waiting || phase == sim::CastlePhase::Running;
    }
    if (world.square > 0) {
        const sim::SquarePhase phase = world.realm->squareRun().phase;
        return phase == sim::SquarePhase::Waiting || phase == sim::SquarePhase::Running;
    }
    return false;
}

namespace {

// The ticket that let him into a run a restart cut short (g_ticketsBack), back in his bag at its
// level, as the Chaos Machine makes one -- or in his vault when the bag has no room. False when
// neither has.
bool ticketBack(sim::Kept& kept, int group, int number, int level) {
    const int32_t item = g_items.itemAt(group, number);
    if (item < 0) return false;
    const content::ItemRow& row = g_items.items[size_t(item)];
    const sim::Held ticket{item, int16_t(level), int16_t(sim::fullDurability(row, level))};
    sim::Satchel bag;
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        if (!kept.hero.slots[slot].empty()) bag.put(slot, kept.hero.slots[slot]);
    }
    if (const int slot = bag.free(g_items, row.width, row.height); slot >= 0) {
        kept.hero.slots[slot] = ticket;
        return true;
    }
    if (const int cell = kept.vault.free(g_items, row.width, row.height); cell >= 0) {
        kept.vault.put(cell, ticket);
        return true;
    }
    return false;
}

}  // namespace

// Where he is, to be written: this world at the tile he stands on, or the world the realm has
// sent him to at its tile. Never inside an event: a Blood Castle left mid-run comes back in its
// town, at the spawn gate, as WebZen logs him in (user.cpp:3147-3150).
server::Store::Row placeOf(const Session& one) {
    server::Store::Row row{one.token, one.world->name, one.world->realm->keptOf(one.player)};
    // A run cut short by the restart, he still in it and not sent anywhere by it: his cloak or
    // invitation back (sim/event.h kCastleGate's Invisibility Cloak 13/18, sim/square.h's Devil's
    // Invitation 14/19). Every save writes from the realm as it stands, so it is given once.
    if (g_ticketsBack && runCutShort(*one.world) && g_due.find(one.token) == g_due.end()) {
        const bool castle = one.world->castle > 0;
        const int level = castle ? one.world->castle : one.world->square;
        if (!ticketBack(row.kept, castle ? 13 : 14, castle ? 18 : 19, level)) {
            core::logError("%s: no room for his %s +%d, cut short by the restart", one.who.c_str(),
                           castle ? "Invisibility Cloak" : "Devil's Invitation", level);
        }
    }
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
        // His Leave ahead of everyone's this tick: kept with his trade's offer back in his bag, his
        // trade is called off (Realm::depart) before a partner's accept queued ahead of it could swap
        // it -- the offer in the partner's bag and in his kept one, an item and its Zen twice.
        one.world->queued.insert(one.world->queued.begin(),
                                 sim::Command{.kind = sim::Command::Kind::Leave, .player = one.player});
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

}  // namespace mu::server
