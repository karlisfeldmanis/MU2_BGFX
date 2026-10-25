// What a monster is where it is met: the numbers of the body the realm itself stands up, not its
// cooked row. Each place is raised as the game raises it -- the map's Realm (its Elites and, on a
// town the Golden Dragon invades, the raid's dragon and minions, which Realm::raise lays out with
// the raid's health and blows), Blood Castle with setCastle, Devil Square's bodies as its realm
// raises them -- and every monster body's numbers are read back.

#include <map>
#include <memory>
#include <tuple>

#include "dump.h"
#include "sim/elite.h"
#include "sim/maps.h"
#include "sim/square.h"
#include "sim/realm.h"

namespace armory {
namespace {

using Key = std::tuple<std::string, int, int32_t, bool>;  // world, castle, breed, elite
std::map<Key, J> g_spawned;
std::map<std::pair<std::string, int>, bool> g_raised;

J statsOf(const sim::Body& b) {
    J o = J::obj();
    J damage = J::arr();
    damage.push(b.stats.minimumDamage);
    damage.push(b.stats.maximumDamage);
    o.set("level", b.level).set("life", b.maxHealth).set("damage", damage).set("defense", b.stats.defense)
        .set("attack_rate", J(double(b.stats.attackRate))).set("defense_rate", J(double(b.stats.defenseRate)))
        .set("swing_ticks", b.swingTicks).set("move_ticks", J(b.speed > 0.0f ? 1.0 / double(b.speed) : 0.0));
    return o;
}

void raiseOnce(const World& w, int castle, uint64_t seed, int* elitesSeen) {
    const sim::MapRow* row = sim::mapOf(w.name);
    sim::Realm realm;
    if (!realm.raise(w.tables.get(), seed, row ? row->arrive[0] : 128, row ? row->arrive[1] : 128)) return;
    if (castle > 0) realm.setCastle(castle);
    const content::Tables& t = *realm.tables();
    std::map<Key, int> counted;
    for (const sim::Body& b : realm.bodies()) {
        if (!b.monster() || b.kind < 0 || size_t(b.kind) >= t.kinds.size()) continue;
        // Devil Square's arenas share one map: a square's own bodies are those whose home is in
        // its arena (sim::inSquareArena), asked as `castle` -square.
        if (castle < 0 && !sim::inSquareArena(-castle, b.homeColumn, b.homeRow)) continue;
        const Key key{w.name, castle, t.kinds[size_t(b.kind)].number, b.elite};
        ++counted[key];
        J& slot = g_spawned[key];
        if (slot.null()) {
            slot = statsOf(b);
            if (b.elite && elitesSeen) ++*elitesSeen;
        }
    }
    for (const auto& [key, n] : counted) {
        if (!std::get<3>(key)) g_spawned[key].set("bodies", n);
    }
}

std::map<std::string, std::unique_ptr<sim::Realm>> g_realms;

}  // namespace

bool onTreasureGround(const World& w, uint32_t kind) {
    auto& realm = g_realms[w.name];
    if (!realm) {
        realm = std::make_unique<sim::Realm>();
        const sim::MapRow* row = sim::mapOf(w.name);
        if (!realm->raise(w.tables.get(), 20261024, row ? row->arrive[0] : 128, row ? row->arrive[1] : 128)) return false;
    }
    for (const content::MonsterNest& nest : w.tables->nests) {
        if (nest.kind != kind) continue;
        for (int r = nest.y1; r <= nest.y2; ++r) {
            for (int c = nest.x1; c <= nest.x2; ++c) {
                if (realm->treasureGround(c, r)) return true;
            }
        }
    }
    return false;
}

const J* spawned(const World& w, int castle, int32_t number, bool elite) {
    const std::pair<std::string, int> place{w.name, castle};
    if (!g_raised[place]) {
        g_raised[place] = true;
        int elites = 0;
        raiseOnce(w, castle, 20261024, &elites);
        // Each rise draws an Elite's breed afresh: raised again until each of the map's strongest
        // has stood once (Realm::riseElite), or forty tries.
        for (uint64_t seed = 1; elite && castle == 0 && seed < 40 && elites < sim::kEliteStrongest; ++seed) {
            raiseOnce(w, castle, seed, &elites);
        }
    }
    const auto it = g_spawned.find(Key{w.name, castle, number, elite});
    if (it == g_spawned.end() && elite) {
        // The first call for a place may not have asked for its Elites: try them now.
        int elites = 0;
        for (const auto& [k, v] : g_spawned) elites += std::get<0>(k) == w.name && std::get<3>(k) ? 1 : 0;
        for (uint64_t seed = 100; castle == 0 && seed < 140 && elites < sim::kEliteStrongest; ++seed) {
            raiseOnce(w, castle, seed, &elites);
        }
        const auto again = g_spawned.find(Key{w.name, castle, number, elite});
        return again == g_spawned.end() ? nullptr : &again->second;
    }
    return it == g_spawned.end() ? nullptr : &it->second;
}

}  // namespace armory
