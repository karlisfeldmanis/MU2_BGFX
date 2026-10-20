// The server's own checks, without a socket (docs/plans/order-plan.md sprint C). Until the server was
// split out of one main.cpp nothing in it could be linked into a test, and nothing was: the
// character store, the account's rules, the chat's allowance and -- the one lockstep stands on --
// that a world the server raises and the mirror a client raises from its Welcome are the same
// realm, tick after tick.
//
// `cmake --build build --target server_test && build/server_test`; it runs in `checks`. It reads
// Lorencia's cooked tables, as sim_test does.

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <thread>

#include <sqlite3.h>
#include <sys/socket.h>

#include "core/files.h"
#include "host.h"
#include "keeper.h"
#include "broker.h"
#include "sim/wear.h"
#include "market_link.h"
#include "net/pass.h"

namespace {

using namespace mu;
using namespace mu::server;

int g_checks = 0, g_failures = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

std::string scratchFile(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / "mu2_server_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / name;
    for (const char* tail : {"", "-wal", "-shm"}) std::filesystem::remove(path.string() + tail);
    return path.string();
}

// ---- the store ----------------------------------------------------------------------------

void testStore() {
    std::printf("store\n");
    Store store;
    std::string error;
    check(store.open(scratchFile("store.db"), error), "the store opens: " + error);
    Store::Row row;
    row.token = 42;
    row.world = "noria";
    row.kept.hero.level = 7;
    row.kept.hero.money = 1234;
    check(store.keep(row), "a character is kept");
    sim::Kept back;
    std::string world;
    check(store.find(42, back, world), "and found again by his token");
    check(world == "noria", "in the world he left");
    check(back.hero.level == 7 && back.hero.money == 1234, "at his level, with his zen");
    check(!store.has(43), "a token never kept is not there");

    check(store.create(7, "abcdef0123456789", "Sevina", sim::Kin::FairyElf, 0, "noria"),
          "a character made on the screen");
    check(store.named("sevina"), "her name is taken, whatever its case");
    check(store.list("abcdef0123456789").size() == 1, "and her account lists her");
    check(store.remove("abcdef0123456789", 7), "she is deleted");
    check(store.list("abcdef0123456789").empty(), "and listed no more");
}

// ---- a save's item numbers across a new item -------------------------------------------------
//
// muclassic, 2026-10-16: the Pendant of Haste went in at row 177 and every saved thing past it
// read as the row above -- his Ring of Ice +2 a Loch's Feather +2, an Apple as Red Spirit Pants.
// A row written before item lists were kept is read by the list without the pendant; one this
// server writes reads back as it was written.
void testItemRows() {
    std::printf("item rows\n");
    const std::string assets = MU2_ASSET_DIR;
    content::Tables tables;
    std::string error;
    check(content::loadTables(assets + "/cooked/lorencia/lorencia.mur", tables, error), "tables: " + error);
    const int32_t haste = tables.itemNamed("NecklaceHaste");
    const int32_t ring = tables.itemNamed("Ring01");     // Ring of Ice
    const int32_t apple = tables.itemNamed("Potion01");  // Apple
    const int32_t sword = tables.itemNamed("Axe01");     // before the pendant, never moved
    check(haste >= 0 && ring > haste && apple > haste && sword >= 0 && sword < haste,
          "the pendant sits before the ring and the apple");

    // An old server's save: its numbers one short past the pendant, written with no list.
    const std::string file = scratchFile("items.db");
    std::remove(file.c_str());
    {
        Store old;
        check(old.open(file, error), "the old store opens: " + error);
        Store::Row row;
        row.token = 9;
        row.kept.hero.slots[sim::kWorn] = sim::Held{ring - 1, 2, 1};
        row.kept.hero.slots[sim::kWorn + 1] = sim::Held{apple - 1, 0, 1};
        row.kept.hero.slots[sim::kWeaponRight] = sim::Held{sword, 3, 20};
        row.kept.vault.put(0, sim::Held{ring - 1, 0, 1});
        check(old.keep(row), "an old save kept");
    }
    Store store;
    check(store.open(file, error) && store.useItems(tables, error), "the store takes its items: " + error);
    sim::Kept back;
    std::string world;
    check(store.find(9, back, world), "the old save found");
    check(back.hero.slots[sim::kWorn].item == ring && back.hero.slots[sim::kWorn].refinement == 2,
          "the Ring of Ice +2 is a Ring of Ice again, not a Loch's Feather");
    check(back.hero.slots[sim::kWorn + 1].item == apple, "the Apple an Apple, not Red Spirit Pants");
    check(back.hero.slots[sim::kWeaponRight].item == sword, "a thing before the pendant unmoved");
    check(back.vault[0].item == ring, "and the vault the same");

    // Written again by this server, it reads back as written, and a second open moves nothing.
    Store::Row again{9, "", back};
    check(store.keep(again), "kept again");
    Store reopened;
    check(reopened.open(file, error) && reopened.useItems(tables, error), "reopened: " + error);
    sim::Kept twice;
    check(reopened.find(9, twice, world), "found again");
    check(twice.hero.slots[sim::kWorn].item == ring && twice.hero.slots[sim::kWorn + 1].item == apple,
          "renumbered once, never twice");
    check(reopened.bookRenumber() == nullptr, "and no book to renumber");
}

// ---- the account's vault -------------------------------------------------------------------
//
// Bug #34, 2026-10-20: a second character of the account opened Baz and found nothing, his
// things in his first's. The vault is the account's: one from before is put together from its
// characters' own, and after that whichever of them is written writes it.
void testAccountVault() {
    std::printf("account vault\n");
    const std::string assets = MU2_ASSET_DIR;
    content::Tables tables;
    std::string error;
    check(content::loadTables(assets + "/cooked/lorencia/lorencia.mur", tables, error), "tables: " + error);
    const int32_t ring = tables.itemNamed("Ring01");
    const int32_t apple = tables.itemNamed("Potion01");
    const int32_t sword = tables.itemNamed("Axe01");
    check(ring >= 0 && apple >= 0 && sword >= 0, "the ring, the apple and the axe");
    Store store;
    check(store.open(scratchFile("vaults.db"), error) && store.useItems(tables, error), "the store opens: " + error);
    const std::string account = "0123456789abcdef";
    check(store.create(11, account, "Gordichan", sim::Kin::DarkKnight, 0, "lorencia") &&
              store.create(12, account, "Merlin", sim::Kin::DarkWizard, 1, "lorencia"),
          "two characters on one account");
    // Kept as before the vault was the account's: written straight to their own rows.
    Store::Row first{11, "lorencia", {}};
    first.kept.vault.put(0, sim::Held{ring, 2, 1});
    first.kept.vault.put(1, sim::Held{sword, 3, 20});
    first.kept.vault.setZen(100);
    Store::Row second{12, "lorencia", {}};
    second.kept.vault.put(0, sim::Held{apple, 0, 1});
    second.kept.vault.setZen(50);
    check(store.keep({first, second}), "both kept");

    // Each keep writes the account's row: the last written is the vault.
    sim::Vault one;
    check(store.vaultOf(account, one), "the account has a vault");
    check(one[0].item == apple && one[1].empty() && one.zen() == 50, "the last written's");

    // Merlin puts the ring in alone; Gordichan, in next, finds it.
    Store::Row merlin{12, "lorencia", {}};
    merlin.kept.vault.put(5, sim::Held{ring, 4, 1});
    merlin.kept.vault.setZen(777);
    check(store.keep(merlin), "Merlin kept");
    sim::Vault two;
    check(store.vaultOf(account, two), "read again");
    check(two[5].item == ring && two[5].refinement == 4 && two.zen() == 777, "Gordichan sees what Merlin left");
    check(two[0].empty(), "and nothing Merlin took out");

    // A character of no account keeps his own, and makes no account's row.
    Store::Row bot{13, "lorencia", {}};
    bot.kept.vault.put(0, sim::Held{sword, 0, 20});
    check(store.keep(bot), "a bot kept");
    sim::Vault none;
    check(!store.vaultOf("", none), "no account, no account's vault");
    sim::Kept back;
    std::string world;
    check(store.find(13, back, world) && back.vault[0].item == sword, "the bot's own vault stays his");
}

// ---- an account from before: its characters' vaults put together -------------------------------
void testVaultMerge() {
    std::printf("vault merge\n");
    const std::string assets = MU2_ASSET_DIR;
    content::Tables tables;
    std::string error;
    check(content::loadTables(assets + "/cooked/lorencia/lorencia.mur", tables, error), "tables: " + error);
    const int32_t ring = tables.itemNamed("Ring01");
    const int32_t apple = tables.itemNamed("Potion01");
    const std::string file = scratchFile("merge.db");
    const std::string account = "fedcba9876543210";
    {
        Store old;
        check(old.open(file, error) && old.useItems(tables, error), "the old store opens: " + error);
        check(old.create(21, account, "Gordichan", sim::Kin::DarkKnight, 0, "lorencia") &&
                  old.create(22, account, "Merlin", sim::Kin::DarkWizard, 1, "lorencia"),
              "two characters");
        Store::Row first{21, "lorencia", {}};
        first.kept.vault.put(0, sim::Held{ring, 2, 1});
        first.kept.vault.put(1, sim::Held{apple, 0, 1});
        first.kept.vault.setZen(100);
        Store::Row second{22, "lorencia", {}};
        second.kept.vault.put(0, sim::Held{apple, 0, 1});
        second.kept.vault.setZen(50);
        check(old.keep({first, second}), "both kept");
    }
    // The server before had no vaults table: what keep wrote there is dropped.
    {
        sqlite3* db = nullptr;
        check(sqlite3_open(file.c_str(), &db) == SQLITE_OK &&
                  sqlite3_exec(db, "DELETE FROM vaults", nullptr, nullptr, nullptr) == SQLITE_OK,
              "the vaults dropped, as before them");
        sqlite3_close(db);
    }
    Store store;
    check(store.open(file, error) && store.useItems(tables, error), "reopened: " + error);
    sim::Vault merged;
    check(store.vaultOf(account, merged), "the account's vault made");
    int rings = 0, apples = 0;
    for (int cell = 0; cell < sim::kVaultCells; ++cell) {
        rings += merged[cell].item == ring;
        apples += merged[cell].item == apple;
    }
    check(rings == 1 && apples == 2, "every thing of both, nothing twice");
    check(merged[0].item == ring && merged[0].refinement == 2, "the fullest's things where they were");
    check(merged.zen() == 150, "and both their Zen");
    sim::Vault again;
    check(store.vaultOf(account, again) && again.zen() == 150, "made once, then read");
}

// ---- the account's and the world's names --------------------------------------------------------

void testNames() {
    std::printf("names\n");
    check(keyShape("abcdef0123456789"), "a 16-letter key is an account's");
    check(!keyShape("short"), "a short one is not");
    check(!keyShape("abcdef01234567../"), "nor one with a path in it");
    check(worldName("lorencia"), "a world is letters");
    check(!worldName("../etc") && !worldName("") && !worldName("Lorencia"),
          "a path, nothing or a capital is not a world");
}

// ---- the chat's allowance ------------------------------------------------------------------------

void testChat() {
    std::printf("chat\n");
    Session one;
    int allowed = 0;
    for (int i = 0; i < 20; ++i) allowed += sayAllowed(one) ? 1 : 0;
    check(allowed == int(kSayBurst), "a burst of lines, then he waits: " + std::to_string(allowed));
}

// ---- the raise: the server's world and a client's mirror -------------------------------------

// Steps the world and the mirror side by side as the server's tick does, comparing the hash
// every client compares (net::stateHash) at the server's cadence.
bool lockstep(sim::Realm& world, sim::Realm& mirror, int ticks, const std::string& what) {
    for (int t = 1; t <= ticks; ++t) {
        for (sim::Realm* realm : {&world, &mirror}) {
            realm->setWallClock(1800000000 + t / 20);
            realm->step();
        }
        if (t % kHashEvery == 0 && net::stateHash(world) != net::stateHash(mirror)) {
            check(false, what + ": the mirror diverged at tick " + std::to_string(t));
            return false;
        }
    }
    check(true, what);
    return true;
}

void testRaise() {
    std::printf("raise\n");
    const std::string assets = MU2_ASSET_DIR;
    std::vector<std::unique_ptr<World>> worlds;
    sim::RealmConfig config;
    net::Hello hello;
    hello.world = "lorencia";
    hello.kin = uint8_t(sim::Kin::DarkKnight);
    hello.level = 1;
    hello.column = 142;
    hello.row = 126;
    hello.weapon = "Axe01";
    World* world = raiseWorld(worlds, hello, assets, config, 20261011, "test", nullptr, 0);
    check(world != nullptr, "the server raises Lorencia");
    if (!world) return;

    content::Tables tables;
    std::string error;
    check(content::loadTables(assets + "/cooked/lorencia/lorencia.mur", tables, error),
          "the mirror reads the same tables: " + error);
    // The rules' print (net::rulesPrint): the same tables the same, a byte moved another.
    {
        content::Tables again;
        check(content::loadTables(assets + "/cooked/lorencia/lorencia.mur", again, error) &&
                  net::rulesPrint(again) == net::rulesPrint(tables) && net::rulesPrint(tables) != 0,
              "the same tables are the same rules");
        std::vector<uint8_t> bytes = core::readFile(assets + "/cooked/lorencia/lorencia.mur");
        bytes.back() ^= 1;
        content::Tables moved;
        content::parseTables(bytes, moved, error);  // read or not, its bytes are printed first
        check(net::rulesPrint(moved) != net::rulesPrint(tables), "a byte of the tables moved is other rules");
        content::Tables devias;
        check(content::loadTables(assets + "/cooked/devias/devias.mur", devias, error) &&
                  net::rulesPrint(devias) != net::rulesPrint(tables),
              "another world's tables are another print");
    }
    sim::Realm mirror;
    check(sim::raiseFrom(mirror, &tables, net::raiseOrder(world->start)),
          "a mirror raises from the Welcome");
    check(net::stateHash(*world->realm) == net::stateHash(mirror), "and is the same realm at once");
    lockstep(*world->realm, mirror, 600, "a new character's world and its mirror, 600 ticks");
    check(world->realm->tick() == mirror.tick() && mirror.tick() >= 600,
          "both stepped: tick " + std::to_string(mirror.tick()));

    // And the check can fail: a mirror raised on another seed is told apart (its nests' dice).
    net::Welcome wrong = world->start;
    wrong.seed += 1;
    sim::Realm stranger, twin;
    sim::raiseFrom(stranger, &tables, net::raiseOrder(wrong));
    sim::raiseFrom(twin, &tables, net::raiseOrder(world->start));
    bool parted = net::stateHash(stranger) != net::stateHash(twin);
    for (int t = 1; t <= 200 && !parted; ++t) {
        for (sim::Realm* realm : {&stranger, &twin}) {
            realm->setWallClock(1800000000);
            realm->step();
        }
        parted = net::stateHash(stranger) != net::stateHash(twin);
    }
    check(parted, "a mirror on another seed is told apart by the hash");

    // All of him carried into another world, as a map change does: the kept path.
    const sim::Kept kept = world->realm->keptOf(world->realm->hero().id);
    net::Hello next = hello;
    next.world = "lorencia";
    World* again = raiseWorld(worlds, next, assets, config, 20261012, "test", &kept, 0);
    check(again != nullptr, "the server raises a world round a kept character");
    if (!again) return;
    sim::Realm keptMirror;
    check(sim::raiseFrom(keptMirror, &tables, net::raiseOrder(again->start)),
          "and a mirror raises from that Welcome");
    lockstep(*again->realm, keptMirror, 600, "a kept character's world and its mirror, 600 ticks");
}

// ---- the guild (docs/game/guild.md) -------------------------------------------------------------

// A welcomed player on a line whose far end nobody reads: what the server sends him goes into
// the socket's buffer, and sessionOfToken finds him open.
std::unique_ptr<Session> player(World* world, uint64_t token, const std::string& name) {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return nullptr;
    auto one = std::make_unique<Session>();
    one->socket = net::Socket(fds[0]);
    one->world = world;
    one->player = world->realm->hero().id;
    one->welcomed = true;
    one->token = token;
    one->name = name;
    return one;
}

void testGuild() {
    std::printf("guild\n");
    const std::string assets = MU2_ASSET_DIR;
    std::string error;
    check(g_store.open(scratchFile("guild.db"), error), "the store opens for guilds: " + error);
    std::vector<std::unique_ptr<World>> worlds;
    sim::RealmConfig config;
    // Devias, once to find where the Guild Master stands and again with the hero at his side.
    net::Hello hello;
    hello.world = "devias";
    hello.kin = uint8_t(sim::Kin::FairyElf);
    hello.level = 150;
    hello.column = 200;
    hello.row = 50;
    World* found = raiseWorld(worlds, hello, assets, config, 1, "test", nullptr, 0);
    check(found != nullptr, "the server raises Devias");
    if (!found) return;
    int column = -1, row = -1;
    for (size_t i = 0; i < found->tables.folk.size(); ++i) {
        if (found->tables.folk[i].number == sim::kGuildMaster) found->realm->folkTile(int(i), &column, &row);
    }
    check(column >= 0, "the Guild Master stands in Devias");
    hello.column = column + 1;
    hello.row = row + 1;
    World* devias = raiseWorld(worlds, hello, assets, config, 2, "test", nullptr, 0);
    net::Hello far = hello;
    far.world = "lorencia";
    far.kin = uint8_t(sim::Kin::DarkWizard);
    far.level = 40;
    far.column = 142;
    far.row = 126;
    far.weapon = "Axe01";
    World* lorencia = raiseWorld(worlds, far, assets, config, 3, "test", nullptr, 0);
    check(devias && lorencia, "and Lorencia, for a member on another map");
    if (!devias || !lorencia) return;

    std::vector<std::unique_ptr<Session>> sessions;
    sessions.push_back(player(devias, 1, "Elora"));
    sessions.push_back(player(lorencia, 2, "Brann"));
    Session& elora = *sessions[0];
    Session& brann = *sessions[1];
    net::GuildAsk ask;
    ask.what = net::GuildWhat::Found;
    ask.name = "ab";
    ask.mark[0] = 0x44;
    guildAsk(elora, ask, sessions);
    check(guildOf(1) == 0, "a name of two letters founds nothing");
    ask.name = "Vanguard";
    guildAsk(brann, ask, sessions);
    check(guildOf(2) == 0, "nor does a level-40 player far from the Guild Master");
    guildAsk(elora, ask, sessions);
    const uint32_t id = guildOf(1);
    check(id != 0 && g_guilds[id].name == "Vanguard", "the master by the Guild Master founds Vanguard");
    guildAsk(brann, ask, sessions);
    check(g_guilds.size() == 1, "a second Vanguard is refused, in any case");

    net::GuildAsk invite;
    invite.what = net::GuildWhat::Invite;
    invite.name = "brann";
    guildAsk(brann, invite, sessions);
    check(g_guildInvites.empty(), "a player in no guild invites no one");
    guildAsk(elora, invite, sessions);
    check(g_guildInvites.count(2) == 1, "the master invites Brann on another map");
    net::GuildAsk accept;
    accept.what = net::GuildWhat::Accept;
    accept.name = "Elora";
    guildAsk(brann, accept, sessions);
    check(guildOf(2) == id && g_guilds[id].members.size() == 2, "and he joins it");

    net::GuildAsk notice;
    notice.what = net::GuildWhat::Notice;
    notice.text = "Kalima tonight";
    guildAsk(brann, notice, sessions);
    check(g_guilds[id].notice.empty(), "a member sets no notice");
    guildAsk(elora, notice, sessions);
    check(g_guilds[id].notice == "Kalima tonight", "the master does");

    // The server starts again: the guild comes back from the disk, whole.
    g_guilds.clear();
    g_guildOf.clear();
    loadGuilds();
    check(guildOf(1) == id && guildOf(2) == id && g_guilds[id].members.size() == 2 &&
              g_guilds[id].notice == "Kalima tonight" && g_guilds[id].mark[0] == 0x44 &&
              g_guilds[id].members.front().name == "Elora",
          "a restarted server reads the guild back: members, master first, notice and mark");

    net::GuildAsk remove;
    remove.what = net::GuildWhat::Remove;
    remove.name = "BRANN";
    guildAsk(elora, remove, sessions);
    check(guildOf(2) == 0 && g_guilds[id].members.size() == 1, "the master removes Brann");

    net::GuildAsk asking;
    asking.what = net::GuildWhat::Ask;
    asking.name = "Elora";
    guildAsk(brann, asking, sessions);
    check(g_guildAsks.count(2) == 1, "Brann asks the master to take him back");
    accept.name = "Brann";
    guildAsk(elora, accept, sessions);
    check(guildOf(2) == id, "and she takes him");

    net::GuildAsk leave;
    leave.what = net::GuildWhat::Leave;
    guildAsk(elora, leave, sessions);
    check(guildOf(1) == id, "a master does not leave his own guild");
    guildAsk(brann, leave, sessions);
    check(guildOf(2) == 0, "a member does");

    net::GuildAsk disband;
    disband.what = net::GuildWhat::Disband;
    disband.name = "Vangard";
    guildAsk(elora, disband, sessions);
    check(g_guilds.count(id) == 1, "a slip of the name disbands nothing");
    disband.name = "vanguard";
    guildAsk(elora, disband, sessions);
    check(g_guilds.empty() && guildOf(1) == 0, "the name typed disbands it");
    g_guildOf.clear();
    loadGuilds();
    check(g_guilds.empty(), "and the disk forgets it");

    // The wire: a mark and a player's guild survive the trip.
    net::GuildMark mark;
    mark.id = 7;
    mark.name = "Ashborn";
    mark.master = "Corwin";
    mark.mark[31] = 0xAB;
    std::vector<uint8_t> bytes;
    net::put(bytes, mark);
    net::GuildMark back;
    check(net::parse(std::vector<uint8_t>(bytes.begin() + 5, bytes.end()), back) && back.mark[31] == 0xAB &&
              back.master == "Corwin" && net::guildPixel(back.mark, 7, 7) == 0xB,
          "a mark crosses the wire, its last pixel the low nibble");
}

// ---- the raid (docs/game/party.md): a party turned into one takes ten -----------------------------

void testRaid() {
    std::printf("raid\n");
    const std::string assets = MU2_ASSET_DIR;
    std::vector<std::unique_ptr<World>> worlds;
    sim::RealmConfig config;
    net::Hello hello;
    hello.world = "lorencia";
    hello.kin = uint8_t(sim::Kin::DarkKnight);
    hello.level = 200;
    hello.column = 142;
    hello.row = 126;
    hello.weapon = "Axe01";
    World* lorencia = raiseWorld(worlds, hello, assets, config, 1, "test", nullptr, 0);
    check(lorencia != nullptr, "the server raises Lorencia for a raid");
    if (!lorencia) return;
    std::vector<std::unique_ptr<Session>> sessions;
    for (int i = 0; i < 12; ++i) sessions.push_back(player(lorencia, uint64_t(100 + i), "Raider" + std::to_string(i)));
    Session& leader = *sessions[0];
    const auto bring = [&](int i) {
        partyAsk(leader, {net::PartyWhat::Invite, sessions[size_t(i)]->name}, sessions);
        partyAsk(*sessions[size_t(i)], {net::PartyWhat::Accept, leader.name}, sessions);
    };
    const auto partyOfLeader = [&]() -> Party* {
        const auto at = g_partyOf.find(leader.token);
        return at != g_partyOf.end() ? &g_parties[at->second] : nullptr;
    };
    for (int i = 1; i <= 5; ++i) bring(i);
    Party* party = partyOfLeader();
    check(party != nullptr && party->members.size() == size_t(sim::kPartyMost) && !party->raid,
          "a party takes five and refuses the sixth");
    if (!party) return;
    partyAsk(*sessions[1], {net::PartyWhat::Raid, {}}, sessions);
    check(!party->raid, "a member does not make it a raid");
    partyAsk(leader, {net::PartyWhat::Raid, {}}, sessions);
    check(party->raid, "the leader does");
    for (int i = 5; i <= 11; ++i) bring(i);
    check(party->members.size() == size_t(sim::kRaidMost), "a raid takes ten and refuses the eleventh");
    partyAsk(leader, {net::PartyWhat::Unraid, {}}, sessions);
    check(party->raid, "a raid of ten is not made a party");
    for (int i = 9; i >= 5; --i) partyAsk(*sessions[size_t(i)], {net::PartyWhat::Leave, {}}, sessions);
    check(party->raid && party->members.size() == 5, "five left, it is a raid still");
    partyAsk(leader, {net::PartyWhat::Unraid, {}}, sessions);
    check(!party->raid, "and its leader makes it a party again");
    for (int i = 1; i <= 4; ++i) partyAsk(*sessions[size_t(i)], {net::PartyWhat::Leave, {}}, sessions);
    check(g_parties.empty() && g_partyOf.empty(), "and it disbands as a party does");
}

// ---- the Golden Invasion told to every map (net::Invasion) ---------------------------------------

void testInvasion() {
    std::printf("invasion\n");
    // The wire: its phase, map, copy and tile survive the trip, and a phase past Standing does not.
    net::Invasion said;
    said.phase = sim::InvasionPhase::Standing;
    said.map = 3;
    said.copy = 2;
    said.column = 199;
    said.row = 168;
    std::vector<uint8_t> bytes;
    net::put(bytes, said);
    std::vector<uint8_t> body;
    net::Kind kind{};
    net::Invasion back;
    check(net::take(bytes, kind, body) == 1 && kind == net::Kind::Invasion && net::parse(body, back) &&
              back == said,
          "where the dragon stands crosses the wire");
    body[0] = 3;
    check(!net::parse(body, back), "and a phase there is not is refused");

    // The server's word: Quiet with no invasion, then the map and the tile it comes down on.
    const std::string assets = MU2_ASSET_DIR;
    std::vector<std::unique_ptr<World>> worlds;
    sim::RealmConfig config;
    net::Hello hello;
    hello.world = "lorencia";
    hello.kin = uint8_t(sim::Kin::DarkKnight);
    hello.level = 200;
    hello.column = 142;
    hello.row = 126;
    hello.weapon = "Axe01";
    World* lorencia = raiseWorld(worlds, hello, assets, config, 7, "test", nullptr, 0);
    check(lorencia != nullptr, "the server raises Lorencia for an invasion");
    if (!lorencia) return;
    std::vector<std::unique_ptr<Session>> sessions;
    check(invasionNow(worlds).phase == sim::InvasionPhase::Quiet, "no dragon, nothing to tell");
    lorencia->realm->invade(true);
    for (int i = 0; i < 4; ++i) tick(*lorencia, sessions, worlds);
    const net::Invasion now = invasionNow(worlds);
    const sim::Body* dragon = lorencia->realm->invader();
    check(now.phase == sim::InvasionPhase::Standing && now.map == 0 && now.copy == 1 && dragon != nullptr &&
              now.column == dragon->homeColumn && now.row == dragon->homeRow && now.column > 0,
          "landed: Lorencia, at (" + std::to_string(now.column) + ", " + std::to_string(now.row) + ")");
}

}  // namespace

// **A trade's partner gone in the tick of the other's Accept** (world.cpp part): he is kept with his
// offer back in his bag, so his Leave must call the trade off before her Accept queued ahead of it
// swaps it -- or the armour is in her bag and in his kept one, the duplication the audit found.
void testDealLeave() {
    std::printf("deal leave\n");
    using Kind = sim::Command::Kind;
    const std::string assets = MU2_ASSET_DIR;
    std::vector<std::unique_ptr<World>> worlds;
    sim::RealmConfig config;
    net::Hello hello;
    hello.world = "lorencia";
    hello.kin = uint8_t(sim::Kin::DarkKnight);
    hello.level = 50;
    hello.column = 142;
    hello.row = 126;
    World* world = raiseWorld(worlds, hello, assets, config, 20261019, "test", nullptr, 0);
    check(world != nullptr, "the server raises Lorencia for a trade");
    if (!world) return;
    sim::Realm& realm = *world->realm;
    const uint32_t a = realm.hero().id;
    const uint32_t b = realm.join(sim::Kin::FairyElf, 50, 143, 126);
    check(b != 0, "a second player joins beside him");
    const int32_t armour = world->tables.itemNamed("ArmorMale01");
    realm.lookAs(a);
    const int armourAt = realm.give(armour, -1, 7);
    check(armourAt >= 0, "he carries an armour +7");
    const auto run = [&](sim::Command c) {
        realm.command(c);
        realm.step();
    };
    run({.kind = Kind::DealAsk, .player = a, .target = b});
    run({.kind = Kind::DealAnswer, .player = b, .a = 1, .target = a});
    run({.kind = Kind::DealPut, .player = a, .a = armourAt, .b = -1});
    for (int i = 0; i < int(sim::kDealHoldTicks); ++i) realm.step();
    run({.kind = Kind::DealAccept, .player = a, .a = 1});
    realm.lookAs(a);
    check(realm.dealAccepted(false), "he has accepted");

    // Her Accept arrives, then his line goes before the tick.
    world->queued.push_back({.kind = Kind::DealAccept, .player = b, .a = 1});
    std::unique_ptr<Session> his = player(world, 777, "Trader");
    his->player = a;
    const sim::Kept kept = realm.keptOf(a);
    bool keptHas = false;
    for (const auto& h : kept.hero.slots) keptHas = keptHas || h.item == armour;
    check(keptHas, "he is kept with the armour back in his bag");
    part(*his);
    std::vector<sim::Command> commands;
    commands.swap(world->queued);
    for (const sim::Command& c : commands) realm.command(c);
    realm.step();
    realm.lookAs(b);
    bool herHas = false;
    for (int slot = 0; slot < sim::kSlots; ++slot) herHas = herHas || realm.satchel()[slot].item == armour;
    check(!herHas, "and she does not have it too");
    check(realm.dealing() == 0u, "the trade is called off");
}


// ---- one market for every realm (docs/game/shared-market.md) ---------------------------------
//
// The keeper as another realm's line would ask it: a listing applied once however often it is
// said, a buy's hold settled or put back, a Collect's returned, a book merged, and the ledger
// that holds all that read back after a restart. Then the line's frames, read back.
void testSharedMarket() {
    std::printf("shared market\n");
    std::string error;
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur", g_items, error),
          "tables: " + error);
    // testGuild's store, open already: a second open would start a second checkpointer.
    check(g_store.useItems(g_items, error), "the store numbers by these items: " + error);
    std::vector<std::unique_ptr<Session>> sessions;
    keeperOpen();
    keeperSample(6);
    const MarketSource central{"Central Europe", "Central Europe#01"};
    const Trader cara{0xCE01, "acct-cara", "Cara", false, "Central Europe"};
    const Trader nils{0x4E01, "acct-nils", "Nils", false, g_region};

    MarketAsk browse{.op = MarketOp::View, .serial = 1, .who = cara};
    browse.ask.what = net::BrokerWhat::Browse;
    MarketAnswer page = keeperServe(browse, central, sessions);
    check(page.view.total > 0 && !page.view.rows.empty(), "another realm browses the keeper's book");
    check(!page.view.rows.empty() && (page.view.rows[0].flags & net::BrokerRow::Elsewhere) != 0,
          "and the keeper's own sellers are elsewhere to it");

    int32_t bless = -1;
    for (size_t i = 0; i < g_items.items.size(); ++i) {
        if (g_items.items[i].label == "Jewel of Bless") bless = int32_t(i);
    }
    check(bless >= 0, "a Bless to list");
    sim::Held jewel;
    jewel.item = std::max(0, bless);
    jewel.durability = 1;
    const int64_t floor = std::max<int64_t>(1, sim::npcValue(g_items, jewel));
    MarketAsk may{.op = MarketOp::MayList, .serial = 2, .who = cara, .floor = floor};
    may.ask.price = floor * 2;
    may.ask.hours = 24;
    check(keeperServe(may, central, sessions).no == uint8_t(sim::BrokerNo::Yes), "she may list");
    may.ask.price = floor - 1;
    check(floor <= 1 || keeperServe(may, central, sessions).no == uint8_t(sim::BrokerNo::Low), "not under a merchant's");

    MarketAsk up{.op = MarketOp::List, .serial = 3, .who = cara};
    up.ask.hours = 24;
    up.listing = sim::Listing{0, cara.token, cara.account, cara.name, jewel, floor * 2, floor, 0, 0};
    const MarketAnswer listed = keeperServe(up, central, sessions);
    check(listed.no == uint8_t(sim::BrokerNo::Yes) && listed.view.mine.size() == 1, "her listing up, in her window");
    keeperServe(up, central, sessions);
    MarketAsk look{.op = MarketOp::View, .serial = 4, .who = cara};
    look.ask.what = net::BrokerWhat::Look;
    check(keeperServe(look, central, sessions).view.mine.size() == 1, "said twice, listed once (rule 3)");
    const uint32_t hers = listed.view.mine.empty() ? 0 : listed.view.mine[0].id;
    check(!listed.view.mine.empty() && (listed.view.mine[0].flags & net::BrokerRow::Elsewhere) == 0,
          "her own listing is not elsewhere to her");
    check(brokerHolds(cara.token) && keeperHolders("Central Europe") == std::vector<uint64_t>{cara.token},
          "she holds something: her realm is told");

    // Nils, on the keeper's realm, buys hers: held, then settled sold.
    MarketAsk buy{.op = MarketOp::Take, .serial = 7, .who = nils};
    buy.ask.listing = hers;
    buy.ask.count = 1;
    const MarketSource local{g_region, ""};
    const MarketAnswer held = keeperServe(buy, local, sessions);
    check(held.no == uint8_t(sim::BrokerNo::Yes) && held.listings.size() == 1, "her listing held for his buy");
    MarketAsk again = buy;
    again.serial = 8;
    check(keeperServe(again, local, sessions).no == uint8_t(sim::BrokerNo::Gone), "and not to be bought twice");
    MarketAsk paid{.op = MarketOp::Settle, .serial = 9, .who = nils, .hold = 7};
    paid.answers = {1};
    keeperServe(paid, local, sessions);
    look.serial = 10;
    const MarketAnswer after = keeperServe(look, central, sessions);
    check(after.view.mine.empty() && after.view.box.size() == 1 && after.view.box[0].zen == floor * 2,
          "sold: her Zen waits in her Collect on her realm");

    // Cara takes her box: one parcel would not fit and is returned.
    MarketAsk box{.op = MarketOp::TakeBox, .serial = 11, .who = cara};
    const MarketAnswer boxed = keeperServe(box, central, sessions);
    check(boxed.parcels.size() == 1, "her box held for a Collect");
    MarketAsk back{.op = MarketOp::Return, .serial = 12, .who = cara, .hold = 11};
    back.parcels = boxed.parcels;
    look.serial = 13;
    check(keeperServe(look, central, sessions).view.box.empty(), "out of the box while held");
    keeperServe(back, central, sessions);
    look.serial = 14;
    check(keeperServe(look, central, sessions).view.box.size() == 1, "what did not reach her back in it");

    // Cara buys a sample: the line goes before she settles, and her server says it works on no hold.
    MarketAsk sample{.op = MarketOp::View, .serial = 15, .who = cara};
    sample.ask.what = net::BrokerWhat::Browse;
    const MarketAnswer samples = keeperServe(sample, central, sessions);
    const uint32_t some = samples.view.rows.empty() ? 0 : samples.view.rows[0].id;
    MarketAsk take{.op = MarketOp::Take, .serial = 16, .who = cara};
    take.ask.listing = some;
    const size_t before = samples.view.total;
    check(keeperServe(take, central, sessions).listings.size() == 1, "a sample held for her");
    keeperLive(central.stream, {16});
    sample.serial = 17;
    check(keeperServe(sample, central, sessions).view.total == before - 1, "a hold still worked on stays out");
    keeperLive(central.stream, {});
    sample.serial = 18;
    check(keeperServe(sample, central, sessions).view.total == before, "one nobody works on goes back (rule 4)");

    // A hold of the keeper's own hands, and the keeper starts again: put back.
    take.serial = 19;
    check(keeperServe(take, local, sessions).listings.size() == 1, "held by this server's own hands");
    keeperKeep();
    keeperOpen();
    sample.serial = 20;
    check(keeperServe(sample, central, sessions).view.total == before, "and back in the book after a restart");
    look.serial = 21;
    check(keeperServe(look, central, sessions).view.box.size() == 1, "her Collect read back from the disk");

    // A realm's own book from before, merged once.
    sim::Book theirs;
    sim::Held armour;
    armour.item = std::max(0, g_items.itemNamed("ArmorMale01"));
    armour.durability = int16_t(std::max(1, sim::maximumDurability(g_items.items[size_t(armour.item)], armour)));
    theirs.list(sim::Listing{0, 0xCE02, "acct-dov", "Dov", armour, 999999, 1, 0, 0}, 12, wallNow());
    MarketAsk merge{.op = MarketOp::Merge, .serial = 22};
    merge.who.region = "Central Europe";
    merge.book = theirs.write();
    keeperServe(merge, central, sessions);
    keeperServe(merge, central, sessions);
    sample.serial = 23;
    sample.who = nils;
    sample.ask.query.sellerName = "Dov";
    const MarketAnswer merged = keeperServe(sample, local, sessions);
    check(merged.view.total == 1, "a realm's old book merged once");
    check(merged.view.rows.size() == 1 && (merged.view.rows[0].flags & net::BrokerRow::Elsewhere) != 0,
          "and its sellers are of that realm");

    // The line's frames.
    MarketAsk round = up;
    round.answers = {1, -1, 3};
    round.parcels = boxed.parcels;
    round.book = merge.book;
    MarketAsk read;
    check(parse(bodyOf(round), read) && read.serial == 3 && read.who.name == "Cara" &&
              read.listing.price == floor * 2 && read.answers == round.answers && read.parcels.size() == 1 &&
              read.book == round.book && read.ask.hours == 24,
          "an ask over the line and back");
    MarketAnswer readAnswer;
    check(parse(bodyOf(held), readAnswer) && readAnswer.listings.size() == 1 &&
              readAnswer.listings[0].id == held.listings[0].id,
          "an answer over the line and back");
    std::vector<uint8_t> stream;
    putLink(stream, LinkKind::Answer, bodyOf(held));
    putLink(stream, LinkKind::Live, bodyOf(std::vector<std::pair<uint64_t, bool>>{{16, false}}));
    std::vector<uint8_t> half(stream.begin(), stream.begin() + 9);
    size_t at = 0;
    LinkKind kind{};
    std::vector<uint8_t> body;
    check(takeLink(half, at, kind, body) == 0 && at == 0, "half a frame waits");
    check(takeLink(stream, at, kind, body) == 1 && kind == LinkKind::Answer, "a whole one is read");
    std::vector<std::pair<uint64_t, bool>> live;
    check(takeLink(stream, at, kind, body) == 1 && kind == LinkKind::Live && parse(body, live) && live.size() == 1 &&
              live[0].first == 16,
          "and the next after it");
    uint64_t streamId = 0, next = 0;
    std::vector<MarketAsk> asks;
    check(readOutbox(writeOutbox(77, 30, {round, up}), streamId, next, asks) && streamId == 77 && next == 30 &&
              asks.size() == 2,
          "an outbox written and read");
}

// The keeper's line over a real socket, a third realm played by the test: let in by its key and
// item list and refused without them, its answers held until the keeper has saved (rule 2), a
// durable ask said twice listed once, and news of its seller's sale sent down its line.
void testMarketLine() {
    std::printf("market line\n");
    std::vector<std::unique_ptr<Session>> sessions;
    const std::string key(64, 'a');
    std::string error;
    check(marketKey(key) && marketServe(45691, error), "the keeper takes lines: " + error);
    const auto pump = [&](net::Socket& probe, std::vector<uint8_t>& in, LinkKind want, std::vector<uint8_t>& body) {
        for (int i = 0; i < 100; ++i) {
            marketPass(sessions);
            probe.receive(in);
            size_t at = 0;
            LinkKind kind{};
            while (takeLink(in, at, kind, body) == 1) {
                if (kind == want) {
                    in.erase(in.begin(), in.begin() + long(at));
                    return true;
                }
            }
            in.erase(in.begin(), in.begin() + long(at));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    const auto send = [](net::Socket& probe, LinkKind kind, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> out;
        putLink(out, kind, body);
        probe.send(out);
        probe.flush();
    };

    // A line with the wrong key is refused.
    net::Socket stranger;
    std::vector<uint8_t> in, body;
    check(stranger.connect("127.0.0.1", 45691, 2.0, error), "a stranger reaches the port: " + error);
    std::string nonce;
    check(pump(stranger, in, LinkKind::Challenge, body) && parse(body, nonce), "and is challenged");
    send(stranger, LinkKind::Hello,
         bodyOf(LinkHello{"South America", 1, itemsFingerprint(g_items), net::passProof(std::string(64, 'b'), nonce, 1)}));
    check(pump(stranger, in, LinkKind::Refused, body), "without the key, refused");

    net::Socket line;
    in.clear();
    check(line.connect("127.0.0.1", 45691, 2.0, error), "a realm reaches the keeper: " + error);
    check(pump(line, in, LinkKind::Challenge, body) && parse(body, nonce), "challenged");
    send(line, LinkKind::Hello,
         bodyOf(LinkHello{"South America", 0xABC, itemsFingerprint(g_items), net::passProof(key, nonce, 1)}));
    check(pump(line, in, LinkKind::Welcome, body), "with the key and our items, let in");
    send(line, LinkKind::Live, bodyOf(std::vector<std::pair<uint64_t, bool>>{}));

    int32_t bless = -1;
    for (size_t i = 0; i < g_items.items.size(); ++i) {
        if (g_items.items[i].label == "Jewel of Bless") bless = int32_t(i);
    }
    sim::Held jewel;
    jewel.item = std::max(0, bless);
    jewel.durability = 1;
    const Trader rosa{0x5A01, "acct-rosa", "Rosa", false, "South America"};
    MarketAsk up{.op = MarketOp::List, .serial = 1, .who = rosa};
    up.ask.hours = 12;
    up.ask.ask = 77;
    up.listing = sim::Listing{0, rosa.token, rosa.account, rosa.name, jewel, 123456, 1, 0, 0};
    send(line, LinkKind::Ask, bodyOf(up));
    for (int i = 0; i < 20; ++i) {
        marketPass(sessions);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    line.receive(in);
    size_t at = 0;
    LinkKind kind{};
    check(takeLink(in, at, kind, body) == 0, "its answer waits for the keeper's save (rule 2)");
    brokerKeep();
    MarketAnswer answer;
    check(pump(line, in, LinkKind::Answer, body) && parse(body, answer) && answer.serial == 1 &&
              answer.no == uint8_t(sim::BrokerNo::Yes) && answer.view.mine.size() == 1 && answer.view.answered == 77,
          "saved: listed, answered with her window");
    send(line, LinkKind::Ask, bodyOf(up));
    marketPass(sessions);
    brokerKeep();
    check(pump(line, in, LinkKind::Answer, body) && parse(body, answer) && answer.view.mine.size() == 1,
          "said again after a lost answer: still one listing");
    const uint32_t hers = answer.view.mine.empty() ? 0 : answer.view.mine[0].id;

    // Who is on there: her dot lights on the keeper's own browse.
    send(line, LinkKind::Present, bodyOf(std::vector<std::pair<uint64_t, bool>>{{rosa.token, false}}));
    for (int i = 0; i < 10; ++i) marketPass(sessions), std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const Trader nils{0x4E01, "acct-nils", "Nils", false, g_region};
    MarketAsk look{.op = MarketOp::View, .serial = 900, .who = nils};
    look.ask.what = net::BrokerWhat::Browse;
    look.ask.query.sellerName = "Rosa";
    const MarketAnswer seen = keeperServe(look, MarketSource{g_region, ""}, sessions);
    check(seen.view.rows.size() == 1 && (seen.view.rows[0].flags & net::BrokerRow::Online) &&
              (seen.view.rows[0].flags & net::BrokerRow::Elsewhere),
          "her listing on the keeper's realm: online, elsewhere");

    // Nils buys it here: the news goes down her realm's line.
    MarketAsk take{.op = MarketOp::Take, .serial = 901, .who = nils};
    take.ask.listing = hers;
    check(keeperServe(take, MarketSource{g_region, ""}, sessions).listings.size() == 1, "Nils takes hers");
    MarketAsk paid{.op = MarketOp::Settle, .serial = 902, .who = nils, .hold = 901};
    paid.answers = {1};
    keeperServe(paid, MarketSource{g_region, ""}, sessions);
    brokerKeep();
    MarketNews news;
    check(pump(line, in, LinkKind::News, body) && parse(body, news) && news.token == rosa.token &&
              news.chat.find("sold for 123,456 Zen") != std::string::npos && news.view.box.size() == 1,
          "her realm told: sold, the Zen in her Collect");
    std::vector<std::pair<uint64_t, bool>> holders;
    check(pump(line, in, LinkKind::Holders, body) && parse(body, holders) && holders.size() == 1 &&
              holders[0].first == rosa.token,
          "and that she holds a Collect");
}

int main() {
    core::logSilence(true);
    testStore();
    testItemRows();
    testAccountVault();
    testVaultMerge();
    testNames();
    testChat();
    testRaise();
    testGuild();
    testDealLeave();
    testRaid();
    testInvasion();
    testSharedMarket();
    testMarketLine();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
