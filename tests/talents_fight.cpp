// The talents A/B (docs/plans/talents.md "Measured"): clang++ -std=c++20 -O2 -I src
//   -DMU2_ASSET_DIR=\"$PWD/out/assets\" tests/talents_fight.cpp out/build-talents/libmu_sim.a
//   out/build-talents/libmu_content_data.a out/build-talents/libmu_core.a out/build-talents/libmu_zstd.a -lz
// The talents A/B as a fight bench. A raid kit's hero (content/raid/party.json), raised on a
// hunting ground, attacks the nearest monster with one skill, with and without the bot's build.
//   talent_fight KIT MAP SKILL SEEDS MINUTES TALENTS(0|1) [BUILD]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/maps.h"
#include "sim/raid_party.h"
#include "sim/realm.h"
#include "sim/talents.h"

using namespace mu;
using Col = std::array<uint8_t, sim::kTalentNodes>;

static sim::TalentRanks build(sim::Kin kin, const std::string& which) {
    const Col F{10, 10, 1, 10, 10, 1, 10, 10, 1}, S{10, 0, 1, 10, 0, 1, 10, 0, 1}, Z{};
    const Col f10{10, 0, 0, 0, 0, 0, 0, 0, 0}, fMore{10, 10, 1, 10, 9, 0, 0, 0, 0};
    sim::TalentRanks r{};
    auto put = [&](std::initializer_list<Col> cols) { size_t c = 0; for (const Col& col : cols) r[c++] = col; };
    if (kin == sim::Kin::DarkKnight) {
        if (which == "dual") put({Z, F, Z, S, F, fMore});
        else if (which == "tank") put({F, Z, F, Z, F, f10});
        else put({F, Z, Z, F, F, f10});
    } else if (kin == sim::Kin::DarkWizard) {
        if (which == "lightning") put({Z, F, F, F, Z, f10});
        else if (which == "spirit") put({Z, Z, S, F, F, fMore});
        else put({F, Z, S, F, Z, fMore});
    } else if (kin == sim::Kin::FairyElf) {
        if (which == "penetration") put({F, F, Z, Z, F, f10});
        else put({F, S, Z, Z, F, fMore});
    }
    return r;
}

int main(int argc, char** argv) {
    if (argc < 7) return 2;
    const std::string kitName = argv[1];
    const int map = std::atoi(argv[2]), skill = std::atoi(argv[3]), seeds = std::atoi(argv[4]);
    const double minutes = std::atof(argv[5]);
    const bool talents = std::atoi(argv[6]) != 0;
    const std::string which = argc > 7 ? argv[7] : "";
    std::vector<sim::RaiderKit> party;
    sim::readParty(std::string(MU2_ASSET_DIR) + "/../../content/raid/party.json", &party);
    const sim::RaiderKit* kit = nullptr;
    for (const auto& one : party) if (one.name == kitName) kit = &one;
    const sim::MapRow* row = sim::mapNumbered(map);
    if (!kit || !row) return 3;
    content::Tables tables;
    std::string error;
    const std::string world = row->world;
    if (!content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/" + world + "/" + world + ".mur", tables, error)) {
        std::printf("%s\n", error.c_str());
        return 4;
    }
    double kills = 0, deaths = 0, drinks = 0, zen = 0, dealt = 0;
    for (int s = 0; s < seeds; ++s) {
        sim::Realm realm;
        if (!realm.raise(&tables, 1000 + s, row->arrive[0], row->arrive[1], kit->kin, kit->level)) return 5;
        realm.benchDressHero(*kit);
        // His potions in the bag, as a hunter carries them: large healing and mana, stacked.
        // An archer's arrows: a quiver in her free hand and more in the bag (group 4: 15 arrows, 7 bolts).
        if (kit->kin == sim::Kin::FairyElf) {
            const int bolts = realm.hero().archer == 2;
            const int quiver = tables.itemAt(4, bolts ? 7 : 15);
            if (quiver >= 0) {
                realm.give(quiver, sim::kWeaponLeft);
                for (int i = 0; i < 6; ++i) realm.give(quiver);
            }
        }
        for (const int number : {3, 3, 3, 3, 6, 6}) {
            const int item = tables.itemAt(14, number);
            if (item >= 0) realm.give(item, -1, 0, sim::stackMost(tables.items[size_t(item)]));
        }
        if (talents) {
            const sim::TalentRanks want = build(kit->kin, which);
            for (int pass = 0; pass < 3; ++pass)
                for (int c = 0; c < sim::kTalentColumnsMost; ++c)
                    for (int i = 0; i < sim::kTalentNodes; ++i) {
                        const int more = want[size_t(c)][size_t(i)] - realm.hero().talents[size_t(c)][size_t(i)];
                        if (more > 0) realm.talentTake(c, i, more);
                    }
        }
        const uint32_t me = realm.hero().id;
        const int64_t zen0 = realm.money();
        const int64_t ticks = int64_t(minutes * 60.0 * 20.0);
        for (int64_t t = 0; t < ticks; ++t) {
            const sim::Body& hero = realm.hero();
            if (t % 5 == 0 && hero.alive()) {
                const sim::Request& now = realm.order();
                bool busy = false;
                if (now.kind == sim::Request::Kind::Attack)
                    for (const sim::Body& b : realm.bodies()) if (b.id == now.target && b.alive()) busy = true;
                if (!busy) {
                    float best = 1e9f; uint32_t at = 0;
                    for (const sim::Body& b : realm.bodies()) {
                        if (!b.monster() || !b.alive()) continue;
                        const float d = std::hypot(b.x - hero.x, b.y - hero.y);
                        if (d < best && d < 30.0f) { best = d; at = b.id; }
                    }
                    if (at) realm.ask({.kind = sim::Request::Kind::Attack, .target = at, .skill = skill});
                }
            }
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Died && h.whom == me && h.who != me) ++kills;
                if (h.what == sim::What::Died && h.who == me) ++deaths;
                if (h.what == sim::What::Drank && h.who == me) ++drinks;
                if (h.what == sim::What::Hit && h.who == me) dealt += h.a;
            }
        }
        zen += double(realm.money() - zen0);
    }
    std::printf("%-7s %-11s map %-2d skill %-3d %-7s kills/h %6.0f  deaths/h %5.1f  potions/h %6.0f  zen/h %8.0f  dmg/h %.3gM\n",
                kitName.c_str(), which.empty() ? "-" : which.c_str(), map, skill, talents ? "talents" : "none",
                kills / seeds * 60 / minutes, deaths / seeds * 60 / minutes, drinks / seeds * 60 / minutes,
                zen / seeds * 60 / minutes, dealt / seeds * 60 / minutes / 1e6);
    return 0;
}
