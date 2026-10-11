// The server's own checks, without a socket (docs/order-plan.md sprint C). Until the server was
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

#include "host.h"

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

}  // namespace

int main() {
    core::logSilence(true);
    testStore();
    testNames();
    testChat();
    testRaise();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
