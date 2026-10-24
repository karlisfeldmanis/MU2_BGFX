// What a Dark Lord's Critical Damage is worth to his party (docs/plans/dark-lord-spells/
// critical-damage.md): a raid kit's level-400 hero (content/raid/party.json) hunts with one skill,
// a level-400 Dark Lord of his party standing by him -- Command and energy spent -- in both runs,
// casting it whenever it lapses in the second alone. The same seeds; the damage an hour, the
// criticals and excellents, and what the hits that were either gained.
//   clang++ -std=c++20 -O2 -I src -DMU2_ASSET_DIR=\"$PWD/out/assets\" tests/keen_bench.cpp
//     out/build/libmu_sim.a out/build/libmu_content_data.a out/build/libmu_core.a
//     out/build/libmu_zstd.a -lz -o out/build/keen_bench
//   keen_bench KIT MAP SKILL SEEDS MINUTES COMMAND ENERGY
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/maps.h"
#include "sim/raid_party.h"
#include "sim/realm.h"
#include "sim/skills.h"

using namespace mu;

struct Result {
    double damage = 0, hits = 0, sharp = 0, sharpDamage = 0, casts = 0, keen = 0, kills = 0;
};

static Result hunt(const content::Tables& tables, const sim::MapRow& map, const sim::RaiderKit& kit, int32_t skill,
                   int seeds, double minutes, int command, int energy, bool cast) {
    Result out;
    for (int s = 0; s < seeds; ++s) {
        sim::Realm realm;
        if (!realm.raise(&tables, 1000 + s, map.arrive[0], map.arrive[1], kit.kin, kit.level)) return out;
        realm.benchDressHero(kit);
        if (skill != 0) realm.learn(skill);
        for (const int number : {3, 3, 3, 3, 6, 6}) {
            const int item = tables.itemAt(14, number);
            if (item >= 0) realm.give(item, -1, 0, sim::stackMost(tables.items[size_t(item)]));
        }
        const uint32_t me = realm.hero().id;
        // The lord, in his party beside him: his points into Command and energy, the skill known.
        const uint32_t lord = realm.join(sim::Kin::DarkLord, 400, realm.hero().column() + 1, realm.hero().row());
        if (lord == 0) return out;
        realm.command({.kind = sim::Command::Kind::Spend, .player = lord, .a = 4, .b = command});
        realm.command({.kind = sim::Command::Kind::Spend, .player = lord, .a = 3, .b = energy});
        const_cast<sim::Body*>(realm.find(lord))->learned |=
            uint64_t(1) << sim::skillIndexOf(sim::skill::kCriticalDamage);
        realm.setParty(me, 9);
        realm.setParty(lord, 9);
        const int64_t ticks = int64_t(minutes * 60.0 * 20.0);
        for (int64_t t = 0; t < ticks; ++t) {
            const sim::Body& hero = realm.hero();
            // He keeps up with the hunt, and keeps it up on him.
            if (t % 20 == 0) {
                sim::Body* stood = const_cast<sim::Body*>(realm.find(lord));
                const float dx = hero.x - stood->x, dy = hero.y - stood->y;
                if (dx * dx + dy * dy > 16.0f) {
                    realm.command({.kind = sim::Command::Kind::Order, .player = lord,
                                   .a = int(sim::Request::Kind::WalkTo), .b = hero.column(), .c = hero.row() + 1});
                }
                if (cast && stood->keenUntil <= realm.tick() + 40) {
                    realm.command({.kind = sim::Command::Kind::Cast, .player = lord, .a = sim::skill::kCriticalDamage});
                }
            }
            if (t % 5 == 0 && hero.alive()) {
                const sim::Request& now = realm.order();
                bool busy = false;
                if (now.kind == sim::Request::Kind::Attack)
                    for (const sim::Body& b : realm.bodies()) if (b.id == now.target && b.alive()) busy = true;
                if (!busy) {
                    float best = 1e9f;
                    uint32_t at = 0;
                    for (const sim::Body& b : realm.bodies()) {
                        // The field's own, not its elites or bosses: a defence over his whole band lays
                        // every blow on the floor, where no bonus shows.
                        if (!b.monster() || !b.alive() || b.maxHealth > 20000) continue;
                        const float d = std::hypot(b.x - hero.x, b.y - hero.y);
                        if (d < best && d < 30.0f) { best = d; at = b.id; }
                    }
                    if (at) realm.ask({.kind = sim::Request::Kind::Attack, .target = at, .skill = skill});
                }
            }
            realm.step();
            if (t == 2400) out.keen += realm.hero().keen;
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Cast && h.who == lord && h.a == sim::skill::kCriticalDamage) ++out.casts;
                if (h.what == sim::What::Died && h.whom == me && h.who != me) ++out.kills;
                if (h.what == sim::What::Hit && h.who == me) {
                    out.damage += h.a;
                    ++out.hits;
                    if (h.critical || h.excellent) {
                        ++out.sharp;
                        out.sharpDamage += h.a;
                    }
                }
            }
        }
    }
    const double perHour = 60.0 / minutes / double(seeds);
    out.damage *= perHour;
    out.hits *= perHour;
    out.sharp *= perHour;
    out.sharpDamage *= perHour;
    out.casts *= perHour;
    out.kills *= perHour;
    out.keen /= double(seeds);
    return out;
}

int main(int argc, char** argv) {
    if (argc < 8) {
        std::printf("keen_bench KIT MAP SKILL SEEDS MINUTES COMMAND ENERGY\n");
        return 2;
    }
    const std::string kitName = argv[1];
    const int mapNumber = std::atoi(argv[2]), skill = std::atoi(argv[3]), seeds = std::atoi(argv[4]);
    const double minutes = std::atof(argv[5]);
    const int command = std::atoi(argv[6]), energy = std::atoi(argv[7]);
    std::vector<sim::RaiderKit> party;
    sim::readParty(std::string(MU2_ASSET_DIR) + "/../../content/raid/party.json", &party);
    const sim::RaiderKit* kit = nullptr;
    for (const auto& one : party) if (one.name == kitName) kit = &one;
    const sim::MapRow* map = sim::mapNumbered(mapNumber);
    if (!kit || !map) return 3;
    content::Tables tables;
    std::string error;
    if (!content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/" + map->world + "/" + map->world + ".mur", tables,
                             error)) {
        std::printf("%s\n", error.c_str());
        return 4;
    }
    const Result bare = hunt(tables, *map, *kit, skill, seeds, minutes, command, energy, false);
    const Result keen = hunt(tables, *map, *kit, skill, seeds, minutes, command, energy, true);
    std::printf("%s (%s, level %d) on %s with skill %d, a lord of Command %d and energy %d by him, %d seed(s) x %.0f min\n",
                kit->name.c_str(), kit->kin == sim::Kin::DarkKnight ? "knight"
                                   : kit->kin == sim::Kin::DarkWizard ? "wizard" : "elf",
                kit->level, map->world, skill, command, energy, seeds, minutes);
    for (const auto& [name, r] : {std::pair{"without", bare}, std::pair{"with", keen}}) {
        std::printf("  %-8s %12.0f damage/h  %7.0f hits/h  %6.0f crit+exc/h (%4.1f%%)  %10.0f of it  %5.0f kills/h  "
                    "%4.0f casts/h  bonus on him %0.0f\n",
                    name, r.damage, r.hits, r.sharp, r.hits > 0 ? 100.0 * r.sharp / r.hits : 0.0, r.sharpDamage,
                    r.kills, r.casts, r.keen);
    }
    std::printf("  gain     %+.1f%% damage, %+.1f%% kills\n", bare.damage > 0 ? 100.0 * (keen.damage / bare.damage - 1) : 0.0,
                bare.kills > 0 ? 100.0 * (keen.kills / bare.kills - 1) : 0.0);
    return 0;
}
